/*
 * The checking allocator. Test support: it needs a hosted C library and is
 * compiled into the test that links elemental::alloc_contract.
 */
#include <elemental/alloc_contract.h>

#include <stdio.h>
#include <stdlib.h>

static void checker_fail(const char *what, const void *ptr, el_layout layout)
{
    fprintf(stderr, "el_alloc_checker: %s: pointer %p, size %zu, alignment %zu\n", what, ptr,
            layout.size, layout.align);
    abort();
}

static void *checker_alloc(void *ctx, el_layout layout)
{
    el_alloc_checker *const checker = ctx;
    if (checker->live == checker->cap) {
        checker_fail("out of records (give the checker a larger array)", NULL, layout);
    }
    void *const ptr = el_alloc(checker->inner, layout);
    if (ptr != NULL) {
        checker->records[checker->live] = (el_alloc_check_record){.ptr = ptr, .layout = layout};
        checker->live++;
    }
    return ptr;
}

static void checker_free(void *ctx, void *ptr, el_layout layout)
{
    el_alloc_checker *const checker = ctx;
    for (size_t i = 0; i < checker->live; i++) {
        el_alloc_check_record *const record = &checker->records[i];
        if (record->ptr != ptr) {
            continue;
        }
        if (record->layout.size != layout.size) {
            fprintf(stderr, "el_alloc_checker: allocated with size %zu\n", record->layout.size);
            checker_fail("freed with a different size", ptr, layout);
        }
        if (record->layout.align != layout.align) {
            fprintf(stderr, "el_alloc_checker: allocated with alignment %zu\n",
                    record->layout.align);
            checker_fail("freed with a different alignment", ptr, layout);
        }
        *record = checker->records[checker->live - 1];
        checker->live--;
        el_free(checker->inner, ptr, layout);
        return;
    }
    checker_fail("freed a pointer that is not a live allocation (never allocated, or freed "
                 "already)",
                 ptr, layout);
}

static const el_allocator_ops checker_ops = {
    .alloc = checker_alloc,
    .free = checker_free,
};

void el_alloc_checker_init(el_alloc_checker *checker, el_allocator inner,
                           el_alloc_check_record *records, size_t cap)
{
    if (checker == NULL || inner.ops == NULL || (records == NULL && cap != 0)) {
        checker_fail("el_alloc_checker_init called with a null argument", NULL,
                     (el_layout){.size = cap, .align = 0});
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
