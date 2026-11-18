#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/ring.h>

#include <stdatomic.h>
#include <string.h>

/* What C++ is told the word is (elemental/ring.h). */
_Static_assert(sizeof(el_ring_word) == sizeof(size_t), "el_ring_word is not a plain word");
_Static_assert(_Alignof(el_ring_word) == _Alignof(size_t), "el_ring_word is not a plain word");

/*
 * head and tail count bytes modulo 2 * cap, not cap. Their difference is
 * then the number of bytes held, 0 to cap: no byte of the storage is kept
 * free to tell full from empty, cap need not be a power of two, and a
 * position is a comparison and a subtraction, not a division (a call to
 * __aeabi_uidivmod on a Cortex-M0).
 *
 * Each word has one writer (docs/adr/0023). A side loads its own word
 * relaxed and the other side's with acquire, and stores with release: what
 * the other side wrote before its store, the bytes among it, is visible
 * once the store is.
 */

static size_t own(const el_ring_word *word)
{
    return atomic_load_explicit(word, memory_order_relaxed);
}

static size_t theirs(const el_ring_word *word)
{
    return atomic_load_explicit(word, memory_order_acquire);
}

static void publish(el_ring_word *word, size_t value)
{
    atomic_store_explicit(word, value, memory_order_release);
}

static size_t held(size_t head, size_t tail, size_t cap)
{
    return tail >= head ? tail - head : tail + (2 * cap - head);
}

static size_t position(size_t index, size_t cap)
{
    return index >= cap ? index - cap : index;
}

/* index + n modulo 2 * cap, for n <= cap, without overflow. */
static size_t advance(size_t index, size_t n, size_t cap)
{
    const size_t to_wrap = 2 * cap - index;
    return n >= to_wrap ? n - to_wrap : index + n;
}

void el_ring_init(el_ring *ring, void *storage, size_t cap)
{
    EL_ASSERT(ring != NULL);
    EL_ASSERT(storage != NULL || cap == 0);
    EL_ASSERT(cap <= SIZE_MAX / 2);
    ring->storage = storage;
    ring->cap = cap;
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    atomic_init(&ring->closed, 0);
}

size_t el_ring_room(const el_ring *ring)
{
    EL_ASSERT(ring != NULL);
    if (own(&ring->closed) != 0) {
        return 0;
    }
    /* A head that moves after this load only adds room. */
    return ring->cap - held(theirs(&ring->head), own(&ring->tail), ring->cap);
}

size_t el_ring_write(el_ring *ring, const void *data, size_t len)
{
    EL_ASSERT(ring != NULL);
    EL_ASSERT(data != NULL || len == 0);
    if (own(&ring->closed) != 0) {
        return 0;
    }
    const size_t cap = ring->cap;
    const size_t tail = own(&ring->tail);
    const size_t room = cap - held(theirs(&ring->head), tail, cap);
    const size_t n = len < room ? len : room;
    if (n == 0) {
        return 0;
    }

    /* Up to the end of the storage, then from its start. */
    const size_t at = position(tail, cap);
    const size_t first = n < cap - at ? n : cap - at;
    memcpy(ring->storage + at, data, first);
    memcpy(ring->storage, (const uint8_t *)data + first, n - first);
    /* The consumer sees the bytes once it sees this. */
    publish(&ring->tail, advance(tail, n, cap));
    return n;
}

void el_ring_close(el_ring *ring)
{
    EL_ASSERT(ring != NULL);
    /* After the last bytes' tail: a consumer that sees this sees them. */
    publish(&ring->closed, 1);
}

size_t el_ring_len(const el_ring *ring)
{
    EL_ASSERT(ring != NULL);
    /* A tail that moves after this load only adds bytes. */
    return held(own(&ring->head), theirs(&ring->tail), ring->cap);
}

size_t el_ring_read(el_ring *ring, void *buf, size_t cap_out)
{
    EL_ASSERT(ring != NULL);
    EL_ASSERT(buf != NULL || cap_out == 0);
    const size_t cap = ring->cap;
    const size_t head = own(&ring->head);
    const size_t len = held(head, theirs(&ring->tail), cap);
    const size_t n = cap_out < len ? cap_out : len;
    if (n == 0) {
        return 0;
    }

    const size_t at = position(head, cap);
    const size_t first = n < cap - at ? n : cap - at;
    memcpy(buf, ring->storage + at, first);
    memcpy((uint8_t *)buf + first, ring->storage, n - first);
    /* The producer may reuse the room once it sees this. */
    publish(&ring->head, advance(head, n, cap));
    return n;
}
