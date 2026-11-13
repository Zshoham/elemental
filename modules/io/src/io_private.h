/*
 * Shared inside io; not installed. Each function's caller has already
 * checked the buffer.
 */
#ifndef ELEMENTAL_IO_PRIVATE_H
#define ELEMENTAL_IO_PRIVATE_H

#include <stddef.h>

#include <elemental/io_buf.h>

/*
 * Append what fits of data[0..len), growing first if the storage can. The
 * memory endpoint's write: EL_OK with 1 <= *n <= len, or EL_ERR_NO_SPACE
 * with *n == 0. `len` is at least 1 and `data` is not in the buffer.
 */
el_status el_buf_append_some_(el_buf *b, const void *data, size_t len, size_t *n);

#endif /* ELEMENTAL_IO_PRIVATE_H */
