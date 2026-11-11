/*
 * The checking allocator. Test support: it needs a hosted C library and is
 * compiled into the test that links elemental::alloc_contract.
 */
#include <elemental/alloc_contract.h>

#include <stdio.h>
#include <stdlib.h>

static void checker_fail(const char *what, const void *ptr, size_t size, size_t align)
{
    fprintf(stderr, "el_alloc_checker: %s: pointer %p, size %zu, alignment %zu\n", what, ptr, size,
            align);
    abort();
}

static void *checker_alloc(void *ctx, size_t size, size_t align)
{
    el_alloc_checker *const checker = ctx;
    if (checker->live == checker->cap) {
        checker_fail("out of records (give the checker a larger array)", NULL, size, align);
    }
    void *const ptr = el_alloc(checker->inner, size, align);
    if (ptr != NULL) {
        checker->records[checker->live] =
            (el_alloc_check_record){.ptr = ptr, .size = size, .align = align};
        checker->live++;
    }
    return ptr;
}

static void checker_free(void *ctx, void *ptr, size_t size, size_t align)
{
    el_alloc_checker *const checker = ctx;
    for (size_t i = 0; i < checker->live; i++) {
        el_alloc_check_record *const record = &checker->records[i];
        if (record->ptr != ptr) {
            continue;
        }
        if (record->size != size) {
            fprintf(stderr, "el_alloc_checker: allocated with size %zu\n", record->size);
            checker_fail("freed with a different size", ptr, size, align);
        }
        if (record->align != align) {
            fprintf(stderr, "el_alloc_checker: allocated with alignment %zu\n", record->align);
            checker_fail("freed with a different alignment", ptr, size, align);
        }
        *record = checker->records[checker->live - 1];
        checker->live--;
        el_free(checker->inner, ptr, size, align);
        return;
    }
    checker_fail("freed a pointer that is not a live allocation (never allocated, or freed "
                 "already)",
                 ptr, size, align);
}

static const el_allocator_ops checker_ops = {
    .alloc = checker_alloc,
    .free = checker_free,
};

void el_alloc_checker_init(el_alloc_checker *checker, el_allocator inner,
                           el_alloc_check_record *records, size_t cap)
{
    if (checker == NULL || inner.ops == NULL || (records == NULL && cap != 0)) {
        checker_fail("el_alloc_checker_init called with a null argument", NULL, cap, 0);
    }
    checker->inner = inner;
    checker->records = records;
    checker->cap = cap;
    checker->live = 0;
}

el_allocator el_alloc_checker_as_allocator(el_alloc_checker *checker)
{
    return (el_allocator){.ctx = checker, .ops = &checker_ops};
}

size_t el_alloc_checker_live(const el_alloc_checker *checker)
{
    return checker->live;
}
