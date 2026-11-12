#include "size_classes.h"

/* The class that serves a layout: the smallest block from 16 bytes up that
 * holds `layout.size` and is a multiple of `layout.align`. -1 if none is
 * large enough. Blocks are aligned to their own size, so the alignment holds
 * too. */
static int class_of(el_layout layout)
{
    size_t block = 16;
    int i = 0;
    while (block < layout.size || block < layout.align) {
        if (++i == SIZE_CLASSES_COUNT) {
            return -1;
        }
        block <<= 1;
    }
    return i;
}

static void *size_classes_alloc(void *ctx, el_layout layout)
{
    size_classes *const sc = ctx;
    const int i = class_of(layout);
    if (i < 0) {
        return NULL;
    }
    void *const reused = sc->free_list[i];
    if (reused != NULL) {
        sc->free_list[i] = *(void **)reused;
        return reused;
    }
    const size_t block = (size_t)16 << i;
    const size_t pad = (size_t)(-(uintptr_t)sc->next & (uintptr_t)(block - 1));
    if (pad > sc->remaining || block > sc->remaining - pad) {
        return NULL;
    }
    void *const fresh = sc->next + pad;
    sc->next += pad + block;
    sc->remaining -= pad + block;
    return fresh;
}

static void size_classes_free(void *ctx, void *ptr, el_layout layout)
{
    size_classes *const sc = ctx;
    const int i = class_of(layout); /* the class alloc chose: no header */
    *(void **)ptr = sc->free_list[i];
    sc->free_list[i] = ptr;
}

static const el_allocator_ops size_classes_ops = {
    .alloc = size_classes_alloc,
    .free = size_classes_free,
};

void size_classes_init(size_classes *sc, void *storage, size_t size)
{
    sc->next = storage;
    sc->remaining = size;
    for (int i = 0; i < SIZE_CLASSES_COUNT; i++) {
        sc->free_list[i] = NULL;
    }
}

el_allocator size_classes_as_allocator(size_classes *sc)
{
    return (el_allocator){.ctx = sc, .ops = &size_classes_ops};
}
