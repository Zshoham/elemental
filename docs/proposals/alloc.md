# Proposal: alloc and io_alloc

Status: Accepted (2026-11-12), with the owner's decisions recorded in
[Decisions](#decisions), and implemented as experimental.
Author, date: design pass for M2, 2026-11-11; accepted 2026-11-12

## As accepted and implemented

The proposal below is kept as written for review, with names brought up to
date. What changed between the draft and the implementation:

- **`io_alloc` is part of `io`, not a library of its own.** The owner
  decided on 2026-11-12 to merge it into `io` behind the Kconfig symbol
  `IO_ALLOC` (default on, `depends on IO && ALLOC`). It builds into
  `elemental::io` only then, and `elemental::io` links `elemental::alloc`
  only then. Everywhere below, "the `io_alloc` library" and the target
  `elemental::io_alloc` are that code and its target as first proposed; the
  header is still `elemental/io_alloc.h`. Statements below that `io` gains
  no dependency, or that `io_alloc` is a separate library, describe the
  draft: with `IO_ALLOC` on, `elemental::io` links `elemental::alloc`.

- **Two arenas, not one** (decision 2): `el_bump_arena`, whose free gives
  nothing back, and `el_stack_arena`, whose free gives back the most recent
  allocation, both in `elemental/alloc_arena.h`. The draft's `el_arena` is
  `el_bump_arena`.
- **The reclaim capability is a level, not a yes or no.** The stack arena
  gives back only the allocation that ends at its top, and not the padding
  before it, so it cannot honestly claim "reclaims on free". The fixture
  has one bounded constructor and a `reclaims` field:
  `EL_ALLOC_CONTRACT_RECLAIMS_NOTHING` (bump arena), `_LATEST` (stack
  arena: freeing the latest allocation lets the same request succeed again,
  indefinitely), `_ANY` (size classes: any order). See
  [modules/alloc/README.md](../../modules/alloc/README.md#the-contract-suite).
- **The overflow check uses no division and no double-width multiply**: each
  operand is split into halves of `size_t`. On the Zephyr ARMv6-M build
  `libelemental_alloc.a` leaves no symbol undefined.
- **The libc backend depends on the existing `PLATFORM_POSIX` probe**, and
  is off in the Zephyr test image. Linux is the only host.
- **The checking allocator ships in `elemental::alloc_contract`**, beside
  the suite, as test support.
- **The sized-free record is [ADR 0012](../adr/0012-sized-free.md).**
- The sanitize preset sets `allocator_may_return_null=1`.



Milestone: [M2](../roadmap.md#m2--allocation). Sketch it starts from:
[architecture.md §6.2](../architecture.md#62-allocation--proposed). Status
code: [ADR 0009](../adr/0009-capacity-status.md).

Labels as in [architecture.md](../architecture.md): **source fact** is
something read in a cited source ([Sources](#sources)); everything else is a
recommendation.

## Problem

A C program that needs dynamic storage calls `malloc`. That is one global
heap, with no bound, no instance to hand to a component, alignment beyond
`max_align_t` only through `aligned_alloc`, and no way to say "this request
uses this 4 KiB and nothing else". Firmware avoids `malloc` and writes a
bump allocator over a static array, once per project, with a different
signature each time. A library that wants to let its caller choose has no
common type to accept.

`alloc` gives that common type: an allocator handle that a component takes
as a parameter, an arena over caller storage, a backend over the C
library's heap, and a contract suite so that an allocator written outside
this repository can show it keeps the contract. `io_alloc` is its first
consumer: a writer that grows.

## Usage examples

The three examples are the ones [M2](../roadmap.md#m2--allocation) requires:
a string, a growable buffer, and an object handed across an API. They exist
to put **sized free** under pressure: `el_free` takes back the size and
alignment that were allocated. Each is written as a consumer would write it
against the header in [Interface model](#interface-model); what each one
costs under sized free, and what it would cost under pointer-only free, is
in [Sized free: the evidence](#sized-free-the-evidence).

### Example 1: a string

A contact keeps its own copy of a name. The name is a pointer and a length,
as everywhere in Elemental ([conventions.md](../conventions.md#storage-size-and-alignment),
rule 4); a terminated copy is made only where libc demands one.

```c
#include <elemental/alloc.h>
#include <elemental/status.h>

#include <stdio.h>
#include <string.h>

typedef struct contact {
    el_allocator allocator;
    char *name; /* name_len bytes, no terminator */
    size_t name_len;
} contact;

void contact_init(contact *c, el_allocator allocator)
{
    c->allocator = allocator;
    c->name = NULL;
    c->name_len = 0;
}

/* Replaces the name. On EL_ERR_NO_SPACE the old name is kept. */
el_status contact_set_name(contact *c, const char *name, size_t len)
{
    char *const copy = el_alloc(c->allocator, len, 1);
    if (copy == NULL) {
        return EL_ERR_NO_SPACE;
    }
    if (len != 0) { /* memcpy from a null pointer is undefined, even for 0 */
        memcpy(copy, name, len);
    }
    el_free(c->allocator, c->name, c->name_len, 1);
    c->name = copy;
    c->name_len = len;
    return EL_OK;
}

void contact_deinit(contact *c)
{
    el_free(c->allocator, c->name, c->name_len, 1);
}

/* fopen() needs a terminated path: a short-lived copy from scratch storage. */
el_status contact_open_card(const contact *c, el_allocator scratch, FILE **out)
{
    *out = NULL;
    /* Cannot overflow: name_len bytes were allocated. */
    const size_t size = c->name_len + sizeof ".vcf";
    char *const path = el_alloc(scratch, size, 1);
    if (path == NULL) {
        return EL_ERR_NO_SPACE;
    }
    if (c->name_len != 0) {
        memcpy(path, c->name, c->name_len);
    }
    memcpy(path + c->name_len, ".vcf", sizeof ".vcf");
    *out = fopen(path, "w");
    el_free(scratch, path, size, 1);
    return *out != NULL ? EL_OK : EL_ERR_IO;
}
```

Points this shows: the initial `NULL`, 0 state needs no special case,
because `el_free` of a null pointer with size 0 is valid; an empty name is a
zero-size allocation, which succeeds; the terminated copy's size is computed
once and sits next to both the allocation and the free.

### Example 2: a growable buffer

A log of sensor readings that grows by doubling.

```c
#include <elemental/alloc.h>
#include <elemental/status.h>

#include <stdint.h>
#include <string.h>

typedef struct reading {
    uint32_t time_ms;
    int16_t value;
} reading;

typedef struct reading_log {
    el_allocator allocator;
    reading *items;
    size_t count;
    size_t cap;
} reading_log;

void reading_log_init(reading_log *log, el_allocator allocator)
{
    log->allocator = allocator;
    log->items = NULL;
    log->count = 0;
    log->cap = 0;
}

el_status reading_log_push(reading_log *log, reading r)
{
    if (log->count == log->cap) {
        /* Cannot overflow: cap * sizeof(reading) bytes are allocated. */
        const size_t cap = log->cap == 0 ? 8 : log->cap * 2;
        reading *const items =
            el_alloc_array(log->allocator, cap, sizeof(reading), _Alignof(reading));
        if (items == NULL) {
            return EL_ERR_NO_SPACE;
        }
        if (log->count != 0) {
            memcpy(items, log->items, log->count * sizeof(reading));
        }
        /* The size freed is the capacity, not the count. */
        el_free(log->allocator, log->items, log->cap * sizeof(reading), _Alignof(reading));
        log->items = items;
        log->cap = cap;
    }
    log->items[log->count++] = r;
    return EL_OK;
}

void reading_log_deinit(reading_log *log)
{
    el_free(log->allocator, log->items, log->cap * sizeof(reading), _Alignof(reading));
}
```

The same thing for bytes is `io_alloc`. Collecting a stream of unknown
length into memory:

```c
#include <elemental/io.h>
#include <elemental/io_alloc.h>

/* Reads `in` to its end into `out`. EL_ERR_NO_SPACE: `out` could not grow,
 * and holds what was read before that. */
el_status read_to_end(el_reader in, el_grow_writer *out)
{
    uint8_t chunk[64];
    for (;;) {
        size_t n = 0;
        el_status status = el_read(in, chunk, sizeof chunk, &n);
        if (status == EL_ERR_END_OF_STREAM) {
            return EL_OK;
        }
        if (status != EL_OK) {
            return status;
        }
        status = el_write_all(el_grow_writer_as_writer(out), chunk, n, NULL);
        if (status != EL_OK) {
            return status;
        }
    }
}

/* A caller, with a bounded arena. */
static uint8_t storage[1024];

el_status handle_upload(el_reader request)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, storage, sizeof storage);

    el_grow_writer body;
    el_grow_writer_init(&body, el_bump_arena_as_allocator(&arena));
    el_status status = read_to_end(request, &body);
    if (status == EL_OK) {
        status = store(el_grow_writer_data(&body), el_grow_writer_len(&body));
    }
    el_grow_writer_deinit(&body);
    el_bump_arena_reset(&arena);
    return status;
}
```

### Example 3: an object handed across an API

A library parses a station description into an object it allocates with
the caller's allocator. The type is opaque: the caller never learns its
size.

```c
/* station.h, the library's public header */
#include <elemental/alloc.h>
#include <elemental/io.h>

typedef struct station station;

/* EL_ERR_NO_SPACE: `allocator` could not supply the object. *out is null on
 * every status but EL_OK. The station keeps `allocator` and uses it to free
 * itself; the allocator must outlive it. */
el_status station_parse(el_allocator allocator, const char *text, size_t len,
                        station **out);
size_t station_sensor_count(const station *s);
/* Writes the station back as text: the caller chooses where it goes. */
el_status station_render(const station *s, el_writer out);
void station_destroy(station *s);
```

```c
/* station.c, the library */
typedef struct sensor {
    uint16_t id;
    int16_t offset;
} sensor;

struct station {
    el_allocator allocator;
    size_t sensor_count;
    sensor *sensors;
};

el_status station_parse(el_allocator allocator, const char *text, size_t len,
                        station **out)
{
    *out = NULL;
    const size_t count = count_lines(text, len); /* from input */

    station *const s = el_alloc(allocator, sizeof *s, _Alignof(station));
    if (s == NULL) {
        return EL_ERR_NO_SPACE;
    }
    /* count comes from input: el_alloc_array checks count * size. */
    sensor *const sensors =
        el_alloc_array(allocator, count, sizeof(sensor), _Alignof(sensor));
    if (sensors == NULL) {
        el_free(allocator, s, sizeof *s, _Alignof(station));
        return EL_ERR_NO_SPACE;
    }
    s->allocator = allocator;
    s->sensor_count = count;
    s->sensors = sensors;
    parse_sensors(text, len, sensors, count);
    *out = s;
    return EL_OK;
}

void station_destroy(station *s)
{
    const el_allocator allocator = s->allocator;
    el_free(allocator, s->sensors, s->sensor_count * sizeof(sensor), _Alignof(sensor));
    el_free(allocator, s, sizeof *s, _Alignof(station));
}
```

```c
/* The consumer: one arena per request, reset when the request is done. */
static uint8_t request_storage[2048];

el_status handle_request(const char *text, size_t len, el_writer reply)
{
    el_bump_arena arena;
    el_bump_arena_init(&arena, request_storage, sizeof request_storage);

    station *s = NULL;
    el_status status = station_parse(el_bump_arena_as_allocator(&arena), text, len, &s);
    if (status == EL_OK) {
        status = station_render(s, reply);
        station_destroy(s);
    }
    el_bump_arena_reset(&arena);
    return status;
}
```

The consumer side reads the same whether `el_free` is sized or not:
`station_destroy` is the library's, and the library already stores
`sensor_count` because it needs it to be useful. The one shape that would
push a size onto the consumer is a buffer allocated by the library and
freed by the caller (`char **out` plus "free it with this size"). The
example does not have one: `station_render` writes into a writer the caller
chooses, and a caller who wants the text in memory passes an
`el_grow_writer`. That is the existing rule that storage is the caller's
([architecture.md §5](../architecture.md#5-resources-and-environmental-effects)),
and it is what removes the hard case.

## Backend example

An allocator written outside this repository: size classes of 16 to 256
bytes carved from caller storage. Freed blocks go on a list per class and
are reused. It has **no per-block header**, because `free` is told the size
and alignment and computes the same class `alloc` chose. It is also the
"example allocator" the M2 acceptance asks for, and the one implementation
in the tree whose correctness depends on the size passed to free.

```c
/* classes.h */
#include <elemental/alloc.h>

#include <stddef.h>
#include <stdint.h>

#define CLASSES_COUNT 5 /* blocks of 16, 32, 64, 128, 256 bytes */

typedef struct classes {
    uint8_t *next; /* unused storage starts here */
    size_t remaining;
    void *free_list[CLASSES_COUNT];
} classes;

void classes_init(classes *c, void *storage, size_t size);
el_allocator classes_as_allocator(classes *c);
```

```c
/* classes.c */
#include "classes.h"

/* The smallest block from 16 bytes up that holds `size` and is a multiple
 * of `align`. Blocks are aligned to their own size, so `align` holds too.
 * -1 if no class is large enough. */
static int class_of(size_t size, size_t align)
{
    size_t block = 16;
    int i = 0;
    while (block < size || block < align) {
        if (++i == CLASSES_COUNT) {
            return -1;
        }
        block <<= 1;
    }
    return i;
}

static void *classes_alloc(void *ctx, size_t size, size_t align)
{
    classes *const c = ctx;
    const int i = class_of(size, align);
    if (i < 0) {
        return NULL;
    }
    void *const reused = c->free_list[i];
    if (reused != NULL) {
        c->free_list[i] = *(void **)reused;
        return reused;
    }
    const size_t block = (size_t)16 << i;
    const size_t pad = (size_t)(-(uintptr_t)c->next) & (block - 1);
    if (pad > c->remaining || block > c->remaining - pad) {
        return NULL;
    }
    void *const fresh = c->next + pad;
    c->next += pad + block;
    c->remaining -= pad + block;
    return fresh;
}

static void classes_free(void *ctx, void *ptr, size_t size, size_t align)
{
    classes *const c = ctx;
    const int i = class_of(size, align); /* the class alloc chose */
    *(void **)ptr = c->free_list[i];
    c->free_list[i] = ptr;
}

static const el_allocator_ops classes_ops = {
    .alloc = classes_alloc,
    .free = classes_free,
};

void classes_init(classes *c, void *storage, size_t size)
{
    c->next = storage;
    c->remaining = size;
    for (int i = 0; i < CLASSES_COUNT; i++) {
        c->free_list[i] = NULL;
    }
}

el_allocator classes_as_allocator(classes *c)
{
    return (el_allocator){.ctx = c, .ops = &classes_ops};
}
```

Neither operation checks its arguments: `el_alloc` and `el_free` guarantee
size ≥ 1, a power-of-two alignment, and a non-null pointer, and check the
returned pointer's alignment. With pointer-only free, this allocator would
need a header in front of each block to find its class, and keeping blocks
aligned to their size makes that header cost a whole block's alignment: a
16-byte block would take 32.

## Why not something that exists

| Candidate | Why it is not enough |
|---|---|
| `malloc`/`free`, `aligned_alloc` | One global heap, no instance to pass, no bound. It is wrapped as a backend here, not replaced. |
| Zephyr `sys_heap` | A concrete heap, for Zephyr only; not an interface a component can accept. Its `sys_heap_init(heap, mem, bytes)` over caller memory is a good backend candidate. *Source fact:* not internally synchronized; 4 or 8 bytes of header per chunk. |
| Pigweed `pw_allocator` | C++ with virtual functions; not callable from C or through a C ABI. *Source fact:* `Deallocate(void *ptr)` takes only the pointer; `Allocate` of size 0 returns `nullptr`. |
| Zig `std.mem.Allocator` | A language's standard library. Its shape (a context pointer and a table; `free` receives the slice and the alignment) is what this proposal copies. |
| TLSF, o1heap, and similar | Concrete allocators, each a good backend behind this interface. None defines an interface. |
| Each library's own `alloc`/`free` callback pair | The thing this replaces: one per library, each with different signatures and rules. |

## Dependencies and cost

| | `alloc` | `alloc_libc` | `io_alloc` |
|---|---|---|---|
| Elemental modules | `status` | `alloc` | `io`, `alloc` |
| Runtime symbols | none | `malloc`, `aligned_alloc`, `free` | `memcpy` (already allowed) |
| Allocation | it is the interface; the arena runs over caller storage | the C library's heap | through the allocator it is given |
| Per-object storage | `el_allocator`: 2 pointers. Each arena: 3 words | none: the handle's `ctx` is null | `el_grow_writer`: 5 words |
| New status codes | none | none | none: returns `EL_ERR_NO_SPACE` ([ADR 0009](../adr/0009-capacity-status.md)) |

`elemental::io` gains no dependency: `io_alloc` depends on `io`, never the
reverse. One documentation change in `io.h` is needed; see
[decision 6](#decisions).

## Contracts

### Ownership and lifetime

- `el_allocator` is a handle: two pointers, passed by value, owning
  nothing. It is valid while the object it was made from is alive and has
  not been moved.
- **Memory is released through the allocator that provided it**, with the
  same size and alignment, or by ending that allocator as a whole
  (`el_bump_arena_reset`). A component that allocates keeps the handle it was
  given and never assumes a default.
- An allocation is valid from `el_alloc` until `el_free` or the end of its
  allocator. The handle must outlive every allocation made through it;
  example 3 states this in `station_parse`.
- The arena borrows its storage until it is no longer used. It holds no
  resource, so it has no `deinit` ([conventions.md](../conventions.md#ownership-and-lifetime),
  rule 5). The arena object may be moved while no handle made from it is in
  use; allocations point into the storage, not into the object.
- `el_bump_arena_reset` ends every allocation from that arena at once. Freeing
  one of them afterwards is a contract violation.
- `el_grow_writer` owns its buffer. `el_grow_writer_deinit` frees it. The
  pointer from `el_grow_writer_data` is valid until the next write or
  `deinit`, because growing moves the bytes.

### Exhaustion

- **`el_alloc` and `el_alloc_array` return `NULL`** when the request cannot
  be satisfied: the allocator is exhausted, the request is larger than it
  can ever hold, the alignment is one it cannot provide, or (for the array
  form) `count * size` overflows. The caller cannot tell these apart and
  does the same for each: use less, or give up. This is the documented
  exception in [conventions.md](../conventions.md#errors), rule 2.
- `NULL` is not permanent: the same request may succeed after something is
  freed or reset.
- A failed request changes nothing: earlier allocations are intact and the
  allocator can satisfy a smaller request if it could before.
- **Functions that allocate as part of other work return `EL_ERR_NO_SPACE`**
  and document what their outputs hold. Each example converts `NULL` to
  that status in one line.

### Alignment

- Every request names its alignment: a power of two, at least 1. Anything
  else is a contract violation. There is no "suitable for any type"
  default.
- No upper limit is part of the interface. An allocator that cannot provide
  an alignment returns `NULL`.
- The returned pointer is a multiple of the alignment. `el_alloc` checks
  this after the operation returns (an implementation postcondition).
- Alignment arithmetic uses masks, never `%`, so nothing pulls in a
  division helper on ARMv6-M.

### Zero-size requests

- **`el_alloc(a, 0, align)` succeeds without consulting the allocator.** It
  returns a non-null pointer, aligned to `align`, that must not be
  dereferenced and need not be unique: the highest address that is a
  multiple of `align`, as Zig does (*source fact*: `allocBytesAligned`
  returns `alignment.backward(maxInt(usize))` for a zero byte count).
- `el_free(a, p, 0, align)` does nothing and does not consult the
  allocator. `p` must be null or that pointer.
- So an operation is never called with size 0, which matches Rust's
  `GlobalAlloc` (*source fact*: a zero-size layout is undefined behavior)
  and Zig's table (`len` "must be greater than zero").
- Why not `NULL`: it would be indistinguishable from exhaustion, and a
  zero count from input data is not a failure (example 3 with an empty
  station). Why not an assertion: input can produce it
  ([conventions.md](../conventions.md#arguments), last row). *Source
  facts:* Pigweed and Zephyr return null for size 0; C leaves it
  implementation-defined; Rust's `Allocator` allows zero-size requests.

### Arrays

`el_alloc_array(a, count, size, align)` allocates `count * size` bytes and
returns `NULL` if the product overflows, without consulting the allocator.
It exists because `count` usually comes from input (example 3), and
[conventions.md](../conventions.md#arguments) requires that arithmetic on
caller-supplied sizes be checked. Freeing needs no array form: the caller
frees `count * size`, which cannot overflow once it was allocated.

### Freeing

- `el_free(a, ptr, size, align)`: `size` and `align` must equal the values
  the allocation was made with (for the array form, `count * size`). An
  allocator may rely on them.
- `ptr` may be null only with `size` 0, which is how "nothing" is passed.
  A null pointer with a nonzero size is a contract violation. This is
  stricter than `free(NULL)`, and example 1 shows that the usual initial
  state (`NULL`, 0) already satisfies it.
- **Freeing a pointer the allocator does not own, or with the wrong size or
  alignment, is a contract violation.** An implementation asserts it where
  the check is cheap: the arena checks that `[ptr, ptr + size)` lies inside
  what it has handed out. Elsewhere it is undefined behavior, as it is for
  `free`, `free_sized`, and Rust's `dealloc` (*source facts*).
- **The arena's free does nothing** beyond that check
  ([decision 2](#decisions)). Its storage comes back only
  through `el_bump_arena_reset`.

### io_alloc

`el_grow_writer` is an `el_writer`. Each write either takes bytes (EL_OK,
growing first if needed), or returns `EL_ERR_NO_SPACE` with nothing taken
when it has no spare room and the allocator refused to grow it.

- Growth doubles the capacity, or takes exactly what the write needs if
  that is larger. If the doubled request fails, it retries once with
  exactly what the write needs, so a bounded allocator is not refused for
  the sake of slack.
- If growth fails but some room remains, the write takes what fits and
  returns `EL_OK`; the next write reports `EL_ERR_NO_SPACE`. That is
  "progress or status, never both"
  ([ADR 0003](../adr/0003-status-and-progress.md)).
- `EL_ERR_NO_SPACE` is not permanent, unlike `EL_ERR_END_OF_STREAM`: the
  same write can succeed after the allocator has room again.
- Growth allocates the new buffer before freeing the old one. Over an arena
  the old buffers are not reclaimed, so a writer that grew to `n` bytes has
  used less than `2n` of the arena. This is the cost of
  [deferring resize](#decisions).

### Concurrency and execution context

- Thread safety: none, per handle; the default in
  [conventions.md](../conventions.md#concurrency-and-execution-context).
  Exception: the libc backend has no state and inherits the C library's
  thread safety, so its handle may be used from several threads. A shared
  arena needs the caller's lock. Thread-safe allocators are deferred.
- Blocking: no operation in this module blocks. A backend that may wait
  says so.
- Interrupt handlers: the arena takes no lock and never blocks, so an arena
  used only by one interrupt handler is safe under the one-context rule.
  It is not called "ISR-safe", because conventions.md defines that as "does
  not allocate". The libc backend must not be used from an interrupt
  handler.

### Contract violations (asserted)

Null `ops`, `alloc`, or `free`; an alignment that is zero or not a power of
two; `el_free` with a null pointer and a nonzero size, or with size 0 and a
pointer that is neither null nor the zero-size pointer; an operation
returning a misaligned pointer; for the arena, a free outside what it has
handed out (including after `el_bump_arena_reset`); `el_grow_writer` used after
`deinit`.

### Exceptions to conventions.md

- Returning `NULL` for exhaustion: the exception the conventions already
  reserve for allocation.
- `uintptr_t` is used, for alignment arithmetic and the zero-size pointer.
  It is optional in C11 and present on every target in range; the build
  fails clearly if it is not.

## Interface model

A handle ([ADR 0002](../adr/0002-interface-model.md), mechanism 2), because
allocators must coexist: an arena for a request and the heap for the rest
of the program are both in example 3's program. The application picks one
per component; configuration only decides which backends exist.

### Header sketch

```c
/* elemental/alloc.h */
typedef struct el_allocator_ops {
    /*
     * Allocate `size` bytes aligned to `align`.
     *
     * Called with: size >= 1; align a power of two.
     * Must: return a pointer to `size` writable bytes, a multiple of
     *       `align`, overlapping no live allocation; or return NULL and
     *       change nothing.
     */
    void *(*alloc)(void *ctx, size_t size, size_t align);

    /*
     * Release an allocation.
     *
     * Called with: ptr not null; ptr, size, align exactly as returned by
     *              and passed to a successful alloc on this ctx, not freed
     *              since.
     * Must: not fail. May reclaim the storage, or not.
     */
    void (*free)(void *ctx, void *ptr, size_t size, size_t align);
} el_allocator_ops;

typedef struct el_allocator {
    void *ctx;
    const el_allocator_ops *ops;
} el_allocator;

void *el_alloc(el_allocator allocator, size_t size, size_t align);
void *el_alloc_array(el_allocator allocator, size_t count, size_t size, size_t align);
void el_free(el_allocator allocator, void *ptr, size_t size, size_t align);

/* elemental/alloc_arena.h: el_bump_arena and el_stack_arena, each with */
void el_bump_arena_init(el_bump_arena *arena, void *storage, size_t cap);
el_allocator el_bump_arena_as_allocator(el_bump_arena *arena);
void el_bump_arena_reset(el_bump_arena *arena);
/* ... and the same three for el_stack_arena. */

/* elemental/alloc_libc.h (backend library elemental::alloc_libc) */
el_allocator el_libc_allocator(void);

/* elemental/io_alloc.h (library elemental::io_alloc) */
typedef struct el_grow_writer {
    el_allocator allocator;
    uint8_t *buf;
    size_t len;
    size_t cap;
} el_grow_writer;

void el_grow_writer_init(el_grow_writer *gw, el_allocator allocator);
el_writer el_grow_writer_as_writer(el_grow_writer *gw);
const uint8_t *el_grow_writer_data(const el_grow_writer *gw);
size_t el_grow_writer_len(const el_grow_writer *gw);
void el_grow_writer_deinit(el_grow_writer *gw);
```

Every function has a named use: `el_alloc`, `el_free` in every example;
`el_alloc_array` in examples 2 and 3; the arena and its reset in examples 2
and 3; `el_libc_allocator` for a program's long-lived objects; the grow
writer in example 2 and as the sink for `station_render`.

The front functions, as `el_read` is the front of `el_reader_ops`:

```c
void *el_alloc(el_allocator allocator, size_t size, size_t align)
{
    EL_ASSERT(allocator.ops != NULL);
    EL_ASSERT(allocator.ops->alloc != NULL && allocator.ops->free != NULL);
    EL_ASSERT(align != 0 && (align & (align - 1)) == 0);

    if (size == 0) {
        return zero_size_ptr(align); /* ~(uintptr_t)(align - 1) */
    }
    void *const ptr = allocator.ops->alloc(allocator.ctx, size, align);
    EL_ASSERT(((uintptr_t)ptr & (align - 1)) == 0);
    return ptr;
}
```

Both operations are required. A null `free` meaning "never reclaims" was
drafted for the arena and dropped (see [review](#three-seat-review)).

**libc backend.** For `align <= _Alignof(max_align_t)` it calls `malloc`.
Above that it calls `aligned_alloc` with the size rounded up to a multiple
of the alignment, because C11 requires that (a corrigendum relaxed it in
C17; rounding is correct under both), returning `NULL` if rounding
overflows. `free` calls `free`. It ignores the size it is given. *Source
fact:* glibc 2.43 added C23's `free_sized` and `free_aligned_sized`, and C23
forbids passing a `malloc` result to `free_aligned_sized` and an
`aligned_alloc` result to `free_sized`. With sized free, the backend knows
the alignment at free time and could choose correctly. It does not use
them now: the C11 baseline has neither, and nothing measured asks for it.

**Deferred within the module**, as the roadmap says: resize in place,
pools, statistics (including an arena high-water mark), thread safety. Each
returns with a consumer. Also not proposed: zeroed allocation (no
consumer), a typed allocation macro (see review), a "take the buffer" call
on the grow writer (example 3 shows the caller-supplied writer instead).

### Naming

Function names follow `el_<module>_<verb>` and the existing pattern of the
interface's front functions (`el_read`, `el_write`): `el_alloc`,
`el_alloc_array`, `el_free`; `el_arena_*`; `el_libc_allocator`;
`el_grow_writer_*`; `el_alloc_contract_fixture`.

Checked on 2026-11-11 against libedit 3.1 (20260512), both the `el_`/`EL_`
names in `/usr/include/histedit.h` and the `el_*` symbols `libedit.so`
exports: **none of the proposed names collides.** Two near misses are
recorded so that later work avoids them: libedit has **`el_resize`** (a
function, and an `EL_RESIZE` macro), so a resize operation added later must
not be called that; and `el_reset`, so the arena's is `el_bump_arena_reset`,
never bare.

`el_grow_writer` rather than `el_buffered_writer` or `el_buf_writer`,
because M7 plans a buffered writer with flush, which is a different thing.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `ALLOC` | y | The module: interface, arena, contract suite | Enabling symbol per module, as `IO` |
| `ALLOC_LIBC` | y, depends on a probed symbol | Programs with a C library heap | A backend needs a platform the toolchain has; probed, so it cannot be selected where it would not build |
| `IO_ALLOC` | y | The grow writer | Enabling symbol per module |

No option changes behavior. Limits (arena size, growth factor) are
parameters or fixed, not configuration. The probed symbol for
`ALLOC_LIBC` (that `<stdlib.h>` declares `aligned_alloc`) is an
[open question](#open-questions), since Zephyr's C libraries differ.

## Verification

**Contract suite** (`modules/alloc/contract`, `ElAllocContract`, copied
from `io_contract`). The fixture is a C struct:

| Fixture member | Kind | Meaning |
|---|---|---|
| `reset` | required | Release what the previous request created |
| `allocator` | required | A fresh allocator that can hold the suite's working set (a stated byte total in at most 64 live allocations, alignments up to 64) |
| `allocator_bounded(limit)` | capability: **bounded** | A fresh allocator that never holds more than `limit` bytes of live requests, and can satisfy one request of 64 bytes |
| `reclaims` | level, with bounded | What free gives back: nothing, the latest allocation, or any (see [As accepted](#as-accepted-and-implemented)) |

Promises checked for every allocator: alignment for every power of two up
to 64; every byte of every allocation writable and keeping its value while
others are made and freed (which also catches overlap); free accepted in
first-in, last-in, and interleaved order; zero-size requests and overflowing
array requests answered without consulting the operation (the suite wraps
the fixture's handle in a counting table); a request for `SIZE_MAX` bytes
returns `NULL`. Bounded: requests beyond the limit return `NULL`; a failed
request leaves earlier allocations intact and smaller requests possible.
Reclaims: a thousand cycles of filling and freeing in varied orders.

| Implementation | Universal | Bounded | Reclaims |
|---|---|---|---|
| Arena | run | run | skipped: reset only |
| libc | run | skipped: unbounded | skipped: not observable without a bound |
| Size classes (example) | run | run | run |

The sanitize preset needs `allocator_may_return_null=1` added to its
`ASAN_OPTIONS`, or AddressSanitizer aborts on the `SIZE_MAX` request
instead of letting `malloc` return `NULL`.

**A checking allocator**, in test support only: it wraps another allocator,
records every live `(ptr, size, align)`, and fails the test on a free whose
size or alignment does not match, a double free, or a leak at the end.
Every test that allocates (io_alloc's, the examples') runs through it. This
is where sized-free mistakes, like freeing the count instead of the
capacity in example 2, are caught.

**Backend-specific tests.** Arena: exact packing and padding; reset; each
death test for its ownership check. libc: alignment above `max_align_t`;
the rounding overflow path. io_alloc: the io contract suite (universal
writer tests; it cannot stall or end) plus its own tests for doubling, the
exact-size retry, a partial write at the limit, `EL_ERR_NO_SPACE` with
nothing taken, and recovery after the arena is reset. Death tests for every
violation listed above.

**Broken implementations** the suite must catch: misaligned results (the
front function's assertion), alignment ignored above 8, overlapping
allocations, an allocation shorter than requested, a bounded allocator that
exceeds its limit, a reclaiming one that leaks a class.

**Portability matrix.** Linux is the only host. Microcontrollers are
reached through Zephyr only.

| Target | Toolchain | Level intended |
|---|---|---|
| Linux x86-64 | GCC, Clang; sanitize preset | Run: unit, contract, death tests |
| Zephyr `native_sim` | Zephyr SDK | Run, once M1 has target tests |
| Zephyr ARMv7-M QEMU board | Zephyr SDK | Emulator run of the arena tests and contract suite (an M2 acceptance item); Zephyr symbol check |
| Zephyr ARMv6-M QEMU board | Zephyr SDK | Zephyr symbol check: `alloc` and `io_alloc` add no external symbol; `alloc_libc` adds the three C library functions, allowlisted with that reason |
| Rust sketch on Linux | rustc, as `tests/interop` | Compiled, linked, run |

## Evidence required before stable

The baseline in [api-stability.md](../process/api-stability.md), and:

- Two consumers not written to demonstrate it. Until async or IPC
  discovery, there are none beyond `io_alloc`
  ([module-plan.md, risks](../module-plan.md#4-risks-and-what-would-change-the-plan)).
- Async and IPC discovery have said what they need from pools, and the
  interface did not have to change for it, or was changed.
- The checking allocator has run over those consumers. Size mismatches it
  found are counted and reported: that is the measured cost of sized free.
- The arena and the contract suite pass on the Zephyr ARMv7-M target.
- The Rust sketch runs both directions (below).

## Open questions

Examples settle everything else here. These need implementation evidence,
not discovery:

All three were settled at acceptance:

1. **The overflow check on ARMv6-M**: split into halves, no helper; the
   Zephyr ARMv6-M symbol check confirms it.
2. **The probe for `ALLOC_LIBC`**: the existing `PLATFORM_POSIX`.
3. **The checking allocator** ships in `elemental::alloc_contract`.

## Sized free: the evidence

| | Sized free (proposed) | Pointer-only free |
|---|---|---|
| Example 1, string | Free with `name_len`, already stored. The terminated scratch copy's size is a local next to both calls. | Same code minus one argument. |
| Example 2, growable buffer | Free with `cap * sizeof`. `cap` is stored anyway. **The one real hazard:** freeing with `count` instead of `cap`. Caught by the checking allocator in tests; undetectable in the libc backend at run time. | No hazard. |
| Example 3, object across an API | Consumer: no difference; `station_destroy` hides it. Library: stores `sensor_count`, which it needs anyway. The hard shape, a buffer freed by the caller, was removed by passing an `el_writer`. | Same consumer code. |
| Backends | Size classes and pools need no header. The libc backend ignores the size. | Headerless allocators impossible. The size-class example would need a header of a full block alignment (a 16-byte block takes 32). |
| Rust `GlobalAlloc` / `Allocator` | Maps one to one: `dealloc(ptr, layout)` passes both. Implementing `el_allocator` in Rust over Rust's allocator needs no header. | A Rust implementation must store each layout in a header to call `dealloc`. |
| C23 | `free_sized`/`free_aligned_sized` exist (*source fact*). | `free`. |

What decides it is an asymmetry. A sized contract can be implemented over
any pointer-only allocator (`malloc`, `sys_heap`, Pigweed) by ignoring two
arguments. A pointer-only contract can be implemented over a sized one
(Rust's, size classes, pools) only by adding a header to every allocation.
The consumer's cost, in the three examples, is one hazard in one example,
and the examples avoided the expensive shape by following an existing rule.

Not hidden: no shipped allocator in M2 reads the size. The arena only
checks it, the libc backend ignores it, and the size-class allocator is an
example. The evidence that the size is worth carrying is the example
backend, the Rust mapping, and the pools that discovery is expected to ask
for.

## What async and IPC discovery may ask of pools

Recorded so discovery can check it, not designed:

- **Fixed-size, bounded pools** for operation state and messages, sized
  from caller storage, so that "what does submission return when full"
  ([architecture.md §6.5](../architecture.md#65-async-and-ipc--discovery-before-design))
  has an answer: `NULL` at the allocator, `EL_ERR_NO_SPACE` at submission.
  Sized free costs nothing here: the block size is fixed.
- **Freeing from another context**: a buffer allocated by a task and freed
  in a completion that runs in an interrupt handler. That is the deferred
  thread-safety question, and it meets the atomics decision of M5
  (single producer, single consumer, no read-modify-write). A pool may need
  a caller-supplied critical section rather than an atomic free list.
- **Lifetimes that outlive the call**: a buffer owned by an in-flight
  operation. The allocator contract already allows this; the question is
  ownership transfer, which belongs to the async contract.
- **A pool as a concrete type** with its own functions and an
  `_as_allocator` handle, like the arena, so that a fast path need not go
  through the table.
- Possibly **resize**, if message assembly grows buffers. Discovery should
  say.

## Relation to Rust's allocators

An outline; the compiled sketch is an M2 implementation deliverable in
`tests/interop`.

*Source facts* (rust-lang/rust `library/core/src/alloc/`): `GlobalAlloc`
has `alloc(&self, layout) -> *mut u8` (null on failure; zero size is
undefined behavior) and `dealloc(&self, ptr, layout)` (layout must be the
one used to allocate); `realloc` has a default of allocate, copy, free.
`Layout::from_size_align` requires a nonzero power-of-two alignment and a
rounded size no larger than `isize::MAX`. `Allocator` allows zero-size
requests and returns `Result<NonNull<[u8]>, AllocError>`; the installed
rustc 1.99.1 has it as unstable, and the fetched `master` marks it stable
from 1.100.0.

1. **`el_allocator` as a Rust `GlobalAlloc`.** A wrapper holding the handle
   implements `alloc` as `el_alloc(a, layout.size(), layout.align())` and
   `dealloc` as `el_free(a, ptr, layout.size(), layout.align())`. Every rule
   lines up: null on failure, power-of-two alignment, size and alignment
   back at free, never size 0. The mismatch is `Sync`: `GlobalAlloc` must be
   callable from any thread, and an arena handle is not. The sketch uses
   the libc backend, or an arena behind a `Mutex`.
2. **A Rust allocator as an `el_allocator`.** Two `extern "C"` functions:
   `alloc` builds a `Layout` (returning null if it is invalid, which covers
   sizes above `isize::MAX`) and calls `std::alloc::alloc`; `free` rebuilds
   the same layout from the size and alignment it is given and calls
   `dealloc`. No header is needed, because free is sized. Neither may panic
   across the boundary ([conventions.md](../conventions.md#abi), rule 7).
3. **What the sketch shows** (`tests/interop/rust_alloc_sketch.rs`,
   `ctest -R interop.rust_alloc`): `GlobalAlloc` implemented over an
   `el_stack_arena` behind a `Mutex`, and C's `el_grow_writer` growing in
   Rust's allocator, with every free checked against the layout it was
   allocated with.

## Three-seat review

The proposal was read from each seat in
[new-module.md](../process/new-module.md#review-three-seats). Friction found,
and what was done:

**Application author.**

- *Strings with terminators.* The first draft of example 1 stored a
  terminated name, so every free was `name_len + 1`, the empty state needed
  `c->name == NULL ? 0 : c->name_len + 1`, and `len + 1` needed an overflow
  check. **Fixed** by following the existing rule (strings are pointer and
  length); the terminated copy became a local scratch allocation with its
  size beside both calls. The interface did not change.
- *Freeing the count instead of the capacity* (example 2). **Not fixable**
  without headers; documented at `el_free`, and caught by the checking
  allocator in tests. It is the measured cost of sized free.
- *A caller-freed buffer from a library* (example 3). **Fixed** by writing
  to a caller-supplied `el_writer`, with `el_grow_writer` for memory.
- *What must stay alive.* Stated: the allocator outlives its allocations;
  `el_bump_arena_reset` ends them all; the grow writer's data pointer moves on
  growth.
- *Telling failures apart.* `NULL` has one meaning to the caller; functions
  convert it to `EL_ERR_NO_SPACE`. Sufficient in all three examples.
- *`sizeof` and `_Alignof` repeated at allocation and free.* Considered a
  typed macro (`EL_ALLOC_OF(a, T)`). **Not added**: only a macro can take a
  type, C++ spells `_Alignof` differently, and no example shows a bug it
  would prevent. Revisit if consumers get the alignment wrong.
- `memcpy(dst, NULL, 0)` being undefined is libc's rule. The examples guard
  it; nothing to change here.

**Backend author.**

- *The operation contract* is two "called with" and "must" lists; size 0,
  bad alignment, and null pointers never reach an operation.
- *A null `free` for allocators that never reclaim* was drafted so the arena
  could omit it. **Dropped**: it adds a rule for one implementation, and the
  arena's free carries the ownership assertion.
- *libc and C11 `aligned_alloc`*: the size must be a multiple of the
  alignment in C11. **Fixed** in the backend sketch by rounding, with an
  overflow check.
- *A fixture that cannot express a capability*: the libc backend cannot be
  bounded, so it leaves both capabilities null and the suite reports them
  skipped, as the io suite does for streams that cannot stall.
- *AddressSanitizer's default* aborts on huge `malloc` requests. **Recorded**
  in Verification: the sanitize preset needs `allocator_may_return_null=1`.

**Maintainer.**

- *Dependency graph*: `alloc` → `status`; `io_alloc` → `io`, `alloc`. No
  edge into `io`, checked by the M2 acceptance item that `elemental::io`
  links without `elemental::alloc`.
- *Status vocabulary*: nothing new.
- *A change to another module's contract*: `io.h` lists four writer
  outcomes, and the grow writer adds `EL_ERR_NO_SPACE`. Raised as
  [decision 6](#decisions) rather than slipped in.
- *Kconfig*: three enabling symbols and a probe; no behavior option.
- *Names*: checked against libedit; `el_resize` and `el_reset` recorded as
  names to avoid.
- *"ISR-safe"* is defined as "does not allocate", so the arena cannot be
  called that, although it is usable from a handler. The header states the
  property instead. If the next module hits the same wording, the definition
  in conventions.md should be revisited.
- *What will be hard to change later*: the `el_free` signature (every call
  site and every backend) and the zero-size behavior. These are the
  decisions below, and sized free gets the ADR the roadmap requires once it
  is decided.

## Decisions

Decided by the owner on 2026-11-12: 1 (a); 2, both arenas (see below); 3 (a);
4 (a); 5 (a); 6 (a). The options as they were put:


1. **Sized free.** Options: (a) `el_free(a, ptr, size, align)`; (b)
   `el_free(a, ptr)`, with headers where an allocator needs them.
   **Recommended: (a).** Evidence: [the table above](#sized-free-the-evidence).
   No cost in examples 1 and 3; one hazard in example 2, which tests catch;
   the size-class backend and the Rust mapping need it to avoid headers.
2. **What the arena's free does.** Options: (a) nothing beyond checking
   ownership, with `el_bump_arena_reset` to reclaim everything; (b) roll back if
   it was the last allocation, as Zig's `FixedBufferAllocator` does (*source
   fact*; it does not reclaim alignment padding). **Recommended: (a).** No
   example reclaims through it: example 2 frees the old buffer after
   allocating the new one, so it is never the last. Only example 3's error
   path (freeing the station after the sensor array failed) would get
   storage back under (b): `sizeof(station)`, which the reset that follows
   returns anyway. Adding rollback later is compatible;
   removing it once callers depend on it is not.
3. **Resize.** Options: (a) deferred, with growth as allocate, copy, free;
   (b) an optional `resize` operation now. **Recommended: (a)**, as the
   roadmap says. Evidence: examples 2 and `io_alloc` grow by allocate, copy,
   free; over an arena that costs under twice the final size until reset,
   and over libc nothing measurable. A later resize must not be named
   `el_resize` (libedit).
4. **Freeing a pointer the allocator does not own.** Options: (a) contract
   violation, asserted where cheap (the arena's range check), undefined
   elsewhere; (b) a required check in every allocator; (c) silently ignored.
   **Recommended: (a).** The libc backend cannot check (b), and (c) hides
   bugs. Only a bug can reach it, so it is an assertion
   ([conventions.md](../conventions.md#arguments)).
5. **Zero-size requests.** Options: (a) succeed with a non-null,
   non-dereferenceable pointer, never consulting the allocator; (b) return
   `NULL`; (c) contract violation. **Recommended: (a).** Example 3 with an
   empty station and example 1 with an empty name reach it from input,
   which rules out (c); (b) cannot be told apart from exhaustion.
6. **Writers may return `EL_ERR_NO_SPACE`.** `io.h` documents four writer
   outcomes. Options: (a) add a fifth: "*n == 0; the writer could not get
   storage; not permanent"; (b) the grow writer reports
   `EL_ERR_END_OF_STREAM`, which is permanent and wrong after a reset.
   **Recommended: (a)**, as [ADR 0009](../adr/0009-capacity-status.md)
   anticipated. `el_write` and `el_write_all` need no code change; existing
   writers are unaffected.

## Sources

Read on 2026-11-11. Statements marked *source fact* come from these.

| Source | Revision | What was read |
|---|---|---|
| Zig standard library | 0.17.1, as installed | `std/mem/Allocator.zig` (`VTable`, `allocBytesAligned`, `free`), `std/heap/FixedBufferAllocator.zig` (`free`, `isLastAllocation`) |
| Pigweed, `pigweed.googlesource.com/pigweed/pigweed` | `65ff9fea215a` (`main`) | `pw_allocator/public/pw_allocator/allocator.h`, `deallocator.h`, `capability.h`, `layout.h` |
| Zephyr, `github.com/zephyrproject-rtos/zephyr` | `3a7ed4fcd557` (`main`) | `include/zephyr/sys/sys_heap.h`, `lib/heap/heap.c`, `lib/heap/heap.h`, `include/zephyr/kernel.h` |
| C23 working draft N3096; proposal N2699 | 2023-04-01; 2021-03-24 | 7.24.3 (zero size), 7.24.3.1 (`aligned_alloc`), 7.24.3.4–5 (`free_sized`, `free_aligned_sized`) |
| glibc | `NEWS`, fetched; 2.44 installed | "Version 2.43" entry; `/usr/include/stdlib.h` declares both functions |
| Rust, `github.com/rust-lang/rust` | `3ef0c22cdb86` (`master`); rustc 1.99.1 installed | `library/core/src/alloc/global.rs`, `mod.rs`, `layout.rs` |
| libedit | 3.1 (20260512), installed | `/usr/include/histedit.h`; `nm -D` of `libedit.so` |

The Pigweed, Zephyr, C23, glibc NEWS, and Rust files were read by a
delegated fact-gathering pass and are quoted from its report; the Zig and
libedit files were read directly. Not verified: anything about these
projects beyond the files listed, and whether the Zig interfaces are
stable.
