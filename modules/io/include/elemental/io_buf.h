/*
 * elemental/io_buf.h - a memory buffer, and the stream that ends in one.
 *
 * An el_buf is storage plus a length: bytes [0, len) of `cap`. It is not a
 * stream. Its owner appends to it, writes into its spare room, reads it, and
 * takes bytes back off its end. el_buf_as_writer() makes it the end of a
 * stream (the memory endpoint), and an adapter such as el_buffered_writer
 * (elemental/io_buffered.h) borrows one to buffer another stream.
 *
 * STATUS: experimental. See docs/process/api-stability.md.
 *
 * Storage is the caller's choice, and everything that takes an el_buf * works
 * with either:
 *
 *   - Fixed: el_buf_init(&b, storage, cap). The buffer borrows `storage`.
 *     There is no deinit: a fixed buffer owns nothing.
 *   - Grown: el_buf *b = el_grow_buf_init(&g, allocator), from
 *     elemental/io_alloc.h (only with the Kconfig symbol IO_ALLOC). The
 *     buffer grows from the allocator, and el_grow_buf_deinit() frees it.
 *
 * Failures: EL_ERR_NO_SPACE only, and never permanent. It means that fixed
 * storage is too small, that the allocator refused, or that a length plus a
 * request overflows size_t (sizes may come from input). Nothing else fails.
 *
 * Ownership and lifetime:
 *
 *   - The caller provides the el_buf object. A fixed buffer borrows its
 *     storage until the buffer is no longer used.
 *   - el_buf_data(), el_buf_data_mut() and el_buf_spare() point into the
 *     storage. Each pointer is valid until the next call that can append,
 *     grow, compact or remove bytes.
 *   - The object may be moved while no handle or adapter refers to it.
 *   - While an adapter borrows the buffer, its owner may only read it
 *     (el_buf_data, el_buf_len, el_buf_room) and roll it back as the
 *     adapter's header allows (elemental/io_buffered.h). Anything else is a
 *     contract violation.
 *
 * The struct layout is public so that callers can allocate it. The fields
 * are not part of the API: use the functions.
 * The `grow` hook is private to io. Only el_grow_buf_init() installs it.
 * It is not an extension point. Code outside io must not set or call it.
 *
 * Concurrency: no internal synchronization; one buffer, one context at a
 * time. A fixed buffer never blocks or allocates, so it may be used from an
 * interrupt handler under that rule. A grown one may be used wherever its
 * allocator may.
 *
 * Contract violations, checked by assertions: a null buffer; null storage
 * with a nonzero capacity; null data with a nonzero length; data that points
 * into the buffer's own storage (growth would move it before the copy);
 * committing more than the room; truncating past the length.
 */
#ifndef ELEMENTAL_IO_BUF_H
#define ELEMENTAL_IO_BUF_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct el_buf {
    uint8_t *data;
    size_t len;
    size_t cap;
    /*
     * Private to io, not an extension point. Only el_grow_buf_init()
     * installs this hook. Code outside io must not set or call it.
     * Null for fixed storage. Otherwise makes room >= min.
     */
    el_status (*grow)(struct el_buf *b, size_t min);
} el_buf;

/*
 * Fixed storage: the `cap` bytes at `storage`, borrowed until the buffer is
 * no longer used. `storage` may be null only if `cap` is 0. An el_buf of all
 * zeros, or one initialized with (NULL, 0), is valid and always full.
 */
void el_buf_init(el_buf *b, void *storage, size_t cap);

/*
 * The contents, bytes [0, len). Null while a grown buffer has not allocated
 * (len is then 0, which every Elemental function accepts with a null
 * pointer). el_buf_data_mut() is the same pointer, for patching bytes
 * already written: a length prefix, a code byte, a line ending.
 */
const uint8_t *el_buf_data(const el_buf *b);
uint8_t *el_buf_data_mut(el_buf *b);

/* Bytes held. */
size_t el_buf_len(const el_buf *b);

/* Free bytes now, without growing. */
size_t el_buf_room(const el_buf *b);

/*
 * Append `len` bytes from `data`, whole or not at all.
 *
 *   EL_OK             All appended.
 *   EL_ERR_NO_SPACE   Nothing appended: fixed storage is too small, the
 *                       allocator refused, or el_buf_len() + len overflows.
 *
 * `data` may be null only if `len` is 0, and must not point into this
 * buffer's storage.
 */
EL_NODISCARD el_status el_buf_append(el_buf *b, const void *data, size_t len);

/*
 * Writing in place. el_buf_reserve() makes el_buf_room() at least `min`,
 * growing if the storage can: EL_OK, or EL_ERR_NO_SPACE with nothing changed
 * (also when el_buf_len() + min overflows). el_buf_spare() is the first free
 * byte, null while a grown buffer has not allocated; el_buf_room() bytes may
 * be written there. el_buf_commit() makes the first `n` of them part of the
 * contents (n <= el_buf_room()).
 */
EL_NODISCARD el_status el_buf_reserve(el_buf *b, size_t min);
uint8_t *el_buf_spare(el_buf *b);
void el_buf_commit(el_buf *b, size_t n);

/*
 * Keep the first `len` bytes (len <= el_buf_len()). The storage is kept.
 * Undoes a partly written record.
 */
void el_buf_truncate(el_buf *b, size_t len);

/*
 * The memory endpoint: a writer whose bytes end up in `b`. One attempt
 * takes what fits, growing first if the storage can:
 *
 *   EL_OK             1 <= *n <= len.
 *   EL_ERR_NO_SPACE   *n == 0: full, and it could not grow. Not permanent:
 *                       the same write succeeds once the owner makes room
 *                       (el_buf_truncate) or the allocator has more.
 *
 * Never EL_ERR_WOULD_BLOCK, EL_ERR_END_OF_STREAM or EL_ERR_IO. A write larger
 * than the room is therefore cut, and el_write_all() reports that as
 * EL_ERR_NO_SPACE with the count that fit.
 */
el_writer el_buf_as_writer(el_buf *b);

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_IO_BUF_H */
