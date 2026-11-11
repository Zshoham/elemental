#include "elemental_config.h"

#include <elemental/assert.h>
#include <elemental/io.h>

#include <stdint.h>

/*
 * The single-attempt functions are the boundary between callers and
 * implementations. Checking both sides here means a backend never has to
 * validate arguments, and a backend that breaks the contract is caught where
 * it happens instead of corrupting its caller.
 */

el_status el_read(el_reader reader, void *buf, size_t cap, size_t *n)
{
    EL_ASSERT(reader.ops != NULL);
    EL_ASSERT(reader.ops->read != NULL);
    EL_ASSERT(n != NULL);
    EL_ASSERT(buf != NULL || cap == 0);

    *n = 0;
    if (cap == 0) {
        return EL_OK;
    }

    size_t got = 0;
    const el_status status = reader.ops->read(reader.ctx, buf, cap, &got);

    EL_ASSERT(got <= cap);
    EL_ASSERT((status == EL_OK) == (got > 0));

    *n = got;
    return status;
}

el_status el_write(el_writer writer, const void *data, size_t len, size_t *n)
{
    EL_ASSERT(writer.ops != NULL);
    EL_ASSERT(writer.ops->write != NULL);
    EL_ASSERT(n != NULL);
    EL_ASSERT(data != NULL || len == 0);

    *n = 0;
    if (len == 0) {
        return EL_OK;
    }

    size_t put = 0;
    const el_status status = writer.ops->write(writer.ctx, data, len, &put);

    EL_ASSERT(put <= len);
    EL_ASSERT((status == EL_OK) == (put > 0));

    *n = put;
    return status;
}

/*
 * Both loops are bounded by `len`: every iteration that continues has moved
 * at least one byte.
 */

el_status el_read_exact(el_reader reader, void *buf, size_t len, size_t *n)
{
    EL_ASSERT(buf != NULL || len == 0);

    uint8_t *const bytes = buf;
    size_t total = 0;
    el_status status = EL_OK;

    while (total < len) {
        size_t got = 0;
        status = el_read(reader, bytes + total, len - total, &got);
        if (status != EL_OK) {
            break;
        }
        total += got;
    }

    EL_ASSERT(total <= len);
    EL_ASSERT((status == EL_OK) == (total == len));

    if (n != NULL) {
        *n = total;
    }
    return status;
}

el_status el_write_all(el_writer writer, const void *data, size_t len, size_t *n)
{
    EL_ASSERT(data != NULL || len == 0);

    const uint8_t *const bytes = data;
    size_t total = 0;
    el_status status = EL_OK;

    while (total < len) {
        size_t put = 0;
        status = el_write(writer, bytes + total, len - total, &put);
        if (status != EL_OK) {
            break;
        }
        total += put;
    }

    EL_ASSERT(total <= len);
    EL_ASSERT((status == EL_OK) == (total == len));

    if (n != NULL) {
        *n = total;
    }
    return status;
}
