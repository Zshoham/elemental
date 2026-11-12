#include "elemental_config.h"

#include <elemental/alloc.h>
#include <elemental/assert.h>

#include <limits.h>
#include <stdint.h>

/*
 * The front functions are the boundary between callers and allocators.
 * Zero sizes and bad alignments are dealt with here, so an operation never
 * sees them, and an operation's result is checked
 * here, so a broken allocator is caught where it returns.
 */

static int is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static void check_handle(el_allocator allocator, el_layout layout)
{
    EL_ASSERT(allocator.ops != NULL);
    EL_ASSERT(allocator.ops->alloc != NULL);
    EL_ASSERT(allocator.ops->free != NULL);
    EL_ASSERT(is_power_of_two(layout.align));
}

/*
 * The pointer returned for a zero-size request: the highest address that is
 * a multiple of `align`. It is never dereferenced and never reaches an
 * allocator. A high address is used, as Zig does, because low addresses
 * are real memory on many microcontrollers.
 */
static void *zero_size_pointer(size_t align)
{
    return (void *)~(uintptr_t)(align - 1);
}

/*
 * Whether a * b overflows; if not, the product in *product. No division
 * and no product wider than size_t, so no compiler helper is needed where
 * the CPU has neither (ARMv6-M has no divide instruction and no 32x32->64
 * multiply): each operand is split into halves, and every partial product
 * fits in a size_t.
 */
static int multiply_overflows(size_t a, size_t b, size_t *product)
{
    enum { half_bits = sizeof(size_t) * CHAR_BIT / 2 };
    const size_t low_mask = ((size_t)1 << half_bits) - 1;
    const size_t a_high = a >> half_bits;
    const size_t b_high = b >> half_bits;

    if (a_high != 0 && b_high != 0) {
        return 1; /* At least 2^half_bits * 2^half_bits. */
    }
    /* One of the two terms is zero; the other is below 2^(2 * half_bits). */
    const size_t cross = a_high * (b & low_mask) + (a & low_mask) * b_high;
    if (cross > low_mask) {
        return 1;
    }
    const size_t low = (a & low_mask) * (b & low_mask);
    const size_t high = cross << half_bits;
    if (low > SIZE_MAX - high) {
        return 1;
    }
    *product = low + high;
    return 0;
}

el_layout el_layout_make(size_t size, size_t align)
{
    EL_ASSERT(is_power_of_two(align));
    return (el_layout){.size = size, .align = align};
}

el_status el_layout_array(el_layout elem, size_t count, el_layout *out)
{
    EL_ASSERT(out != NULL);
    EL_ASSERT(is_power_of_two(elem.align));

    size_t total = 0;
    if (multiply_overflows(count, elem.size, &total)) {
        /* A layout no allocator can satisfy: a caller that ignores the
         * status gets NULL from el_alloc(), not a short allocation. */
        *out = (el_layout){.size = SIZE_MAX, .align = elem.align};
        return EL_ERR_NO_SPACE;
    }
    *out = (el_layout){.size = total, .align = elem.align};
    return EL_OK;
}

void *el_alloc(el_allocator allocator, el_layout layout)
{
    check_handle(allocator, layout);

    if (layout.size == 0) {
        return zero_size_pointer(layout.align);
    }
    void *const ptr = allocator.ops->alloc(allocator.ctx, layout);
    EL_ASSERT(((uintptr_t)ptr & (layout.align - 1)) == 0);
    return ptr;
}

void el_free(el_allocator allocator, void *ptr, el_layout layout)
{
    check_handle(allocator, layout);

    if (layout.size == 0) {
        EL_ASSERT(ptr == NULL || ptr == zero_size_pointer(layout.align));
        return;
    }
    EL_ASSERT(ptr != NULL);
    allocator.ops->free(allocator.ctx, ptr, layout);
}
