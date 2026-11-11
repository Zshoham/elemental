/*
 * size_classes - an example allocator: blocks of 16 to 256 bytes carved from
 * caller-provided storage, reused through one free list per size.
 *
 * This is what an allocator author writes: a state struct, the two
 * operations of el_allocator_ops, and a function that hands out a handle.
 * It is also the allocator in this repository whose correctness depends on
 * sized free (docs/adr/0012-sized-free.md). A block carries no header: free
 * is told the size and alignment, and computes from them the same class
 * alloc chose. With pointer-only free, every block would need a header to
 * find its class, and keeping blocks aligned to their size would make that
 * header cost a whole block's alignment.
 *
 *   alloc: the smallest class of 16, 32, 64, 128, or 256 bytes that holds
 *          the size and is a multiple of the alignment. A freed block of
 *          that class if there is one, otherwise a new block from the
 *          storage, aligned to its own size. NULL when neither exists, or
 *          when no class is large enough.
 *   free:  pushes the block onto its class's list. Storage never moves
 *          between classes.
 *
 * Storage: the caller provides the size_classes object and its storage and
 * keeps both alive while the allocator, a handle, or an allocation made
 * from it is in use. Nothing to release.
 *
 * Concurrency: none. One context at a time.
 *
 * Not part of Elemental's API: an example, named without the el_ prefix.
 */
#ifndef SIZE_CLASSES_H
#define SIZE_CLASSES_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIZE_CLASSES_COUNT 5 /* 16, 32, 64, 128, 256 bytes */

typedef struct size_classes {
    uint8_t *next; /* unused storage starts here */
    size_t remaining;
    void *free_list[SIZE_CLASSES_COUNT];
} size_classes;

void size_classes_init(size_classes *sc, void *storage, size_t size);

el_allocator size_classes_as_allocator(size_classes *sc);

#ifdef __cplusplus
}
#endif

#endif /* SIZE_CLASSES_H */
