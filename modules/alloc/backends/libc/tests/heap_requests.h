#ifndef HEAP_REQUESTS_H
#define HEAP_REQUESTS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The test is linked with --wrap=malloc and --wrap=realloc, so that the
 * backend's calls come here first: each records the size it was asked
 * for and passes the call on to the C library unchanged.
 */
extern size_t heap_requests_malloc_size;
extern size_t heap_requests_realloc_size;

void *__real_malloc(size_t size);
void *__real_realloc(void *ptr, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_realloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* HEAP_REQUESTS_H */
