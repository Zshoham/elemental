# Proposal: time

Status: Accepted (2026-11-16), with the owner's [decisions](#decisions),
and implemented as experimental: [modules/time](../../modules/time/README.md).
Author, date: design pass for M4, 2026-11-16; accepted the same day

Milestone: [M4](../roadmap.md#m4--time-and-logging). Plan entry:
[module-plan.md](../module-plan.md#added-by-this-plan). The representation
is [ADR 0020](../adr/0020-time-representation.md), accepted with this
document. `log` is the first consumer ([proposal](log.md)).

Labels as in [architecture.md](../architecture.md): **Source fact** is
something read in a cited source ([Sources](#sources)), **Measured** is a
number produced in this pass with its method, and everything else is a
recommendation. The examples were compiled and run against a prototype
before the module existed ([Verification](#verification)); what the module
itself was run on is in [build.md](../build.md#what-is-verified).

## As accepted and implemented

The proposal below is kept as written for review. What changed between it
and [modules/time](../../modules/time/README.md):

- **The Zephyr clock is in the repository** (decision 2):
  `el_zephyr_clock()` in `elemental/time_zephyr.h`, library
  `elemental::time_zephyr`, selectable only in a Zephyr build through the
  new probed symbol `PLATFORM_ZEPHYR`. The [backend example](#backend-example)
  below is that code. The example backend under `examples/` is the
  stepping clock the [Verification](#verification) section names.
- **The contract suite has five tests**, two of them needing the optional
  `advance` capability. The stepping clock leaves it null; the manual,
  POSIX, and Zephyr clocks have it.
- **`time_zephyr` leaves one symbol undefined**, `z_impl_k_uptime_ticks`,
  allowed only in that configuration
  ([allowed-undefined.TIME_ZEPHYR.txt](../../tests/zephyr/allowed-undefined.TIME_ZEPHYR.txt)).
  `time` itself leaves none.
- **The conversions' agreement with division** is tested in the repository
  for 400 random values at each of thirteen frequencies, on the host and on
  the emulated boards; the 2.4 million values below were the prototype's.

## Problem

[architecture.md](../architecture.md#logic-separated-from-effects) says
time is a parameter: "a module that needs 'now' is given a timestamp or a
clock operation by its caller". Nothing says what that parameter is.

Three things in the roadmap need it: a log record's timestamp (M4), a
timeout in IPC correlation (M8), and the echo server with a timeout that
async discovery is measured on (M9). Without one definition each would
invent a callback and a unit, and a test of any of them would have to
sleep.

ISO C has no monotonic clock. POSIX has `clock_gettime`, Zephyr has
`k_uptime_ticks`, and a freestanding target has whatever counter the board
has.

## Usage examples

### Example 1: how long something took

The common case: read a clock twice, and print the difference.

```c
/* Copy a stream and say how long it took, by whatever clock the caller has. */
EL_NODISCARD static el_status timed_copy(el_reader in, el_writer out, el_clock clock,
                                         el_buf *report)
{
    const el_ticks start = el_clock_now(clock);
    size_t copied;
    EL_TRY(copy_stream(in, out, &copied));
    const el_ticks took = el_clock_now(clock) - start;

    uint32_t us;
    const uint64_t seconds = el_ticks_to_seconds(took, el_clock_ticks_per_second(clock), &us);
    return el_fmt_printf(report, "%zu bytes in %llu.%06u s\n", copied,
                         (unsigned long long)seconds, (unsigned)us);
}

    status = timed_copy(in, out, el_posix_clock(), &report);
```

It printed `19 bytes in 0.000017 s`. Subtraction is plain integer
arithmetic: a clock never goes back, so the later reading is the larger.
The division is in one named call, on the path that prints.

### Example 2: a timeout, and a test of it that takes no time

The awkward case: code whose behavior depends on time passing, and a test
that must not sleep. The manual clock is advanced by the test's own fake
peer, so "250 ms later" is exact and instant.

```c
/* The application's own failure. Negative statuses are the application's. */
enum { LINK_ERR_TIMED_OUT = -1 };

/*
 * Read `len` bytes, giving up once `timeout_ms` has passed without them.
 * `*got` says how far it came. Never waits on its own: `in` stalls or it
 * does not.
 */
EL_NODISCARD static el_status read_with_timeout(el_reader in, void *buf, size_t len, size_t *got,
                                                el_clock clock, uint32_t timeout_ms)
{
    const el_ticks deadline =
        el_clock_now(clock) + el_ticks_from_ms(timeout_ms, el_clock_ticks_per_second(clock));

    *got = 0;
    while (*got < len) {
        size_t n;
        const el_status status = el_read(in, (char *)buf + *got, len - *got, &n);
        if (status == EL_ERR_WOULD_BLOCK) {
            EL_ENSURE(el_clock_now(clock) < deadline, LINK_ERR_TIMED_OUT);
            continue; /* a real program waits for the stream here */
        }
        EL_TRY(status);
        *got += n;
    }
    return EL_OK;
}
```

```c
/* A peer that sends two bytes and then nothing, each attempt taking 1 ms. */
typedef struct slow_peer {
    el_manual_clock *clock;
    size_t left;
} slow_peer;

static el_status slow_peer_read(void *ctx, void *buf, size_t cap, size_t *n)
{
    slow_peer *const peer = ctx;
    (void)cap;
    el_manual_clock_advance(peer->clock, 1);
    if (peer->left == 0) {
        return EL_ERR_WOULD_BLOCK;
    }
    *(char *)buf = 'x';
    *n = 1;
    peer->left--;
    return EL_OK;
}

static const el_reader_ops slow_peer_ops = {.read = slow_peer_read};

    el_manual_clock clock;
    el_manual_clock_init(&clock, 1000); /* a tick is a millisecond */

    slow_peer peer = {&clock, 2};
    const el_reader in = {&peer, &slow_peer_ops};

    char buf[4];
    size_t got;
    const el_status status =
        read_with_timeout(in, buf, sizeof(buf), &got, el_manual_clock_as_clock(&clock), 250);

    assert(status == LINK_ERR_TIMED_OUT);
    assert(got == 2);
    /* Two reads that delivered, then stalls until the deadline: exactly. */
    assert(el_clock_now(el_manual_clock_as_clock(&clock)) == 250);
```

A deadline is a value, and passing it is `>=`. With 64-bit ticks there is
no wrap to handle: at one tick per nanosecond the count lasts 584 years.

**Friction found.** `el_clock_ticks_per_second(clock)` appears wherever a
duration meets a human unit. A function that converts often reads it once.
That is the price of a frequency the clock states at run time, and it is
what [ADR 0020](../adr/0020-time-representation.md) weighs.

## Backend example

Zephyr's uptime, as someone outside this repository would write it. Run on
the Cortex-M0+ board: 10 000 ticks per second there.

```c
#include <zephyr/kernel.h>

#include <elemental/time.h>

static el_ticks zephyr_now(void *ctx)
{
    ARG_UNUSED(ctx);
    return (el_ticks)k_uptime_ticks();
}

static uint32_t zephyr_ticks_per_second(void *ctx)
{
    ARG_UNUSED(ctx);
    return CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

static const el_clock_ops zephyr_ops = {
    .now = zephyr_now,
    .ticks_per_second = zephyr_ticks_per_second,
};

/* The kernel's uptime, in its ticks. Holds no state; safe from an ISR. */
el_clock zephyr_clock(void)
{
    const el_clock handle = {NULL, &zephyr_ops};
    return handle;
}
```

Reading it is one kernel call and no arithmetic. That is the point of
letting the clock keep its own unit: *source fact*, Zephyr's own
`k_uptime_get()` is `k_ticks_to_ms_floor64(k_uptime_ticks())`, a
conversion on every read.

## Why not something that exists

- **`clock_gettime`, `k_uptime_get`, a board's counter.** Each is one
  platform's. Code written against one cannot be tested with a fake, which
  is the manual clock's whole job.
- **A plain callback, `uint64_t (*now)(void)`.** No context pointer, so no
  manual clock without a global, and no way to state the unit.
- **A timestamp as a parameter, no clock.** Right for a function that
  needs "now" once, and still how a pure function takes it. A logger
  stamps every record, so it holds the clock.
- **`mlog`.** Its records carry no time; the backend adds one.

## Dependencies and cost

| | |
|---|---|
| Elemental modules | `status` (assertions only: nothing here returns a status) |
| Runtime symbols (freestanding) | None. *Measured:* see below |
| Allocation | None |
| Per-object storage | `el_clock`: two words. `el_manual_clock`: 16 bytes. The POSIX and Zephyr clocks have no object |
| New status codes | None. `EL_ERR_TIMED_OUT` stays with IPC correlation ([module-plan.md](../module-plan.md)); Example 2 uses the application's own |

*Measured* on 2026-11-16: the prototype built for `nucleo_g0b1re_renode`
(Cortex-M0+) by the Zephyr SDK 1.0.1's GCC 14.3.0 with Zephyr's flags for
the board (`-Os`), assertions trapping. Text bytes per object:

| Object | Bytes | Holds |
|---|---|---|
| `time` | 54 | `el_clock_now`, `el_clock_ticks_per_second` |
| `time_convert` | 286 | `el_ticks_from_ms`, `el_ticks_to_seconds` |
| `time_manual` | 100 | the manual clock |

**Conversion without a compiler helper.** On 32-bit Arm, `/` and `*` on a
64-bit operand are calls into the compiler's runtime. *Measured*, the two
conversions written both ways, linked alone for the Cortex-M0+:

| | Conversions | From libgcc | External symbols |
|---|---|---|---|
| With `/`, `%`, `*` | 84 | 504 (`__udivmoddi4`, `__aeabi_uldivmod`, `__aeabi_lmul`, `__clzsi2`, `__lshrdi3`) | `__aeabi_uldivmod`, `__aeabi_lmul` |
| Shift and subtract, 16-bit halves | 282 | 0 | none |

The second is what is proposed, as `fmt` and `alloc` already do
([fmt proposal](fmt.md)): the allowed-symbols list stays at `memcpy` and
`memmove`, and an image that has no other 64-bit division is 306 bytes
smaller. An image that already links the helpers would be 198 bytes
smaller the other way. It is 64 loop steps per division, on a path that
prints or sets a timeout up, not one that runs per tick. *Measured:* the
loop agrees with the compiler's 128-bit division for 2.4 million values
over twelve frequencies from 1 to 2³²−1, the edges included.

## Contracts

- **Ownership and lifetime:** a clock handle borrows its object for as
  long as the handle is used, as every handle does. The POSIX clock has no
  object. Nothing is released.
- **Errors:** none. Reading a monotonic clock cannot fail, so `now`
  returns its result ([conventions.md](../conventions.md#errors), rule 3).
  A time source that can fail, such as a calendar clock on a bus, is a
  different interface and not proposed.
- **What a reading means:** ticks since an origin the clock chooses.
  Readings of one clock compare and subtract. Readings of two clocks do
  not, even at the same frequency.
- **Monotonic:** a reading is never less than an earlier one. It may be
  equal. Whether the clock runs while the system is suspended is the
  backend's to document.
- **Frequency:** constant for the life of the clock, at least 1, at most
  2³²−1 ticks per second.
- **Overflow:** none in practice. `el_manual_clock_advance` past
  `UINT64_MAX` is a contract violation.
- **Rounding:** `el_ticks_from_ms` rounds up, so a timeout never ends
  early. `el_ticks_to_seconds` rounds down, so a printed time has
  happened.
- **Concurrency:** the front functions add no state. A clock is as safe to
  read from two contexts as its implementation: the POSIX and Zephyr
  clocks are, the manual clock is one context at a time.
- **Execution context:** `now` must not block. The manual clock and the
  conversions are interrupt-safe under the one-context rule. No function
  here sleeps or waits: there is no sleep in this module.
- **Contract violations**, asserted: a null `ops` or operation; a null
  object or output; a frequency of 0, from an implementation or passed to
  a conversion.
- **Exceptions to docs/conventions.md:** none.

## Interface model

A handle, [ADR 0002](../adr/0002-interface-model.md)'s second mechanism: a
program has several clocks at once (the system's in production, a manual
one in each test), and code that takes one must not know which.

```c
/* elemental/time.h */
typedef uint64_t el_ticks;

typedef struct el_clock_ops {
    /* Must: never return less than an earlier call returned. Not block. */
    el_ticks (*now)(void *ctx);
    /* Must: return the same value on every call, at least 1. */
    uint32_t (*ticks_per_second)(void *ctx);
} el_clock_ops;

typedef struct el_clock {
    void *ctx;
    const el_clock_ops *ops;
} el_clock;

el_ticks el_clock_now(el_clock clock);
uint32_t el_clock_ticks_per_second(el_clock clock);

el_ticks el_ticks_from_ms(uint32_t ms, uint32_t ticks_per_second);
uint64_t el_ticks_to_seconds(el_ticks ticks, uint32_t ticks_per_second, uint32_t *us);

/* elemental/time_manual.h */
void el_manual_clock_init(el_manual_clock *clock, uint32_t ticks_per_second);
void el_manual_clock_advance(el_manual_clock *clock, el_ticks ticks);
el_clock el_manual_clock_as_clock(el_manual_clock *clock);

/* elemental/time_posix.h, library elemental::time_posix */
el_clock el_posix_clock(void); /* CLOCK_MONOTONIC, 1 000 000 000 ticks per second */
```

The frequency is an operation and not a field of the table, because the
manual clock's is chosen per object. The front function for it checks the
implementation's result (at least 1). The front function for `now` cannot
check monotonicity without state of its own; the contract suite does.

Names were checked against `histedit.h`: none collide.

## Configuration

| Option | Default | Use case | Why not the linker / a parameter |
|---|---|---|---|
| `TIME` | on | The module | As every module |
| `TIME_POSIX` | on where `PLATFORM_POSIX` | The POSIX backend, off in the Zephyr image | It needs `<time.h>`'s `clock_gettime` |
| `TIME_ZEPHYR` | on where `PLATFORM_ZEPHYR` | The Zephyr backend (decision 2) | It needs Zephyr's kernel header |

No option changes behavior. The frequency is the clock's, at run time.

## Verification

- **Contract tests** (`ElTimeContract`, `time_contract`): the frequency is
  at least 1 and the same on every call; a thousand readings never go
  back; with the capability *advance*, after the fixture makes at least
  `n` ticks pass the reading has grown by at least `n`.
- **Implementations to run it against:** the manual clock (advance is
  exact), the POSIX clock (advance sleeps), and one example clock under
  `examples/` that is neither: a clock that steps by a fixed amount on
  every reading, which cannot be advanced from outside and so leaves the
  capability null.
- **Backend-specific tests:** the manual clock reads 0 at first and
  exactly what it was advanced by; the POSIX clock states 10⁹.
- **Unit tests:** the conversions at the edges (0, one tick below and above
  a second, `UINT64_MAX`, frequencies 1 and 2³²−1), and against the
  compiler's division on the host.
- **Death tests:** each contract violation above.
- **Broken on purpose:** a clock that goes back, a frequency that changes,
  a frequency of 0.

| Target | Toolchain | Level intended |
|---|---|---|
| x86-64 Linux | GCC, Clang, Clang with sanitizers | Run, all of the above |
| `native_sim` | host GCC, 32-bit | Run, without the POSIX backend |
| `mps2/an385`, `nucleo_g0b1re_renode` | Zephyr SDK GCC | Emulator run of the unit and contract tests (manual and example clocks); symbol check |
| `qemu_cortex_m0` | Zephyr SDK GCC | Symbol check |

What was run for this proposal, on a prototype outside the repository:
both examples on x86-64 (GCC 16.2.1, `-std=c11` with the repository's
warnings as errors), the headers as C++11, the backend example on
`nucleo_g0b1re_renode` under Renode 1.17.0 (an **emulator run** on the
modified model), and the measurements above.

## Evidence required before stable

As [api-stability.md](../process/api-stability.md). Specific to `time`:
two consumers that were not written to demonstrate it (`log` is one; IPC
or async timeouts the other), and a clock on real hardware.

## Decisions

1. **The representation is 64-bit ticks of the clock's own frequency**,
   not a fixed unit such as nanoseconds. *Owner, 2026-11-16.*
   [ADR 0020](../adr/0020-time-representation.md).
2. **A Zephyr clock ships in M4**, as `time_zephyr`, behind a probed
   `PLATFORM_ZEPHYR`. *Owner, 2026-11-16.* The roadmap's M4 named only the
   manual and POSIX clocks; microcontrollers are Zephyr targets
   ([ADR 0010](../adr/0010-zephyr-targets.md)), so it is the clock every
   target program will use, and the contract suite runs against a real
   clock on both Arm boards. It is the backend example above, moved into
   the repository; the example backend kept under `examples/` is the
   stepping clock.
3. **`el_ticks_from_ms` is included**, though its consumer in the roadmap
   is M8 or M9 and today it has Example 2. A recommendation, accepted with
   the proposal as a whole: without it every caller writes the multiply,
   divide, and rounding by hand.

Also recommendations accepted with the proposal, not asked one by one:
conversions without a compiler helper; `uint32_t` for the frequency; no
sleep, no wall time, no calendar
([module-plan.md](../module-plan.md#considered-and-rejected)).

## Open questions

- A signed duration. `deadline - now` wraps when the deadline has passed,
  so code compares before it subtracts. A saturating "ticks until" waits
  for a consumer that needs the remainder (M9's poll timeout, likely).
- Milliseconds out (`el_ticks_to_ms`): no consumer yet.
- A frequency above 2³²−1 ticks per second: a cycle counter on a fast
  core. Such a clock scales its reading down.

## Sources

- Zephyr v4.5.0 (`41d10b5973`), read in the local workspace:
  `include/zephyr/kernel.h` (`k_uptime_ticks`, `k_uptime_get`),
  `kernel/Kconfig` (`SYS_CLOCK_TICKS_PER_SEC`, default 100, or 10 000 for
  a tickless kernel), `include/zephyr/logging/log_ctrl.h`
  (`log_set_timestamp_func(getter, freq)`).
- `clock_gettime(2)`, Linux man-pages: `CLOCK_MONOTONIC` is not settable
  and is not affected by jumps of the system time.
- `mlog`, the owner's working copy, read in full on 2026-11-16.
