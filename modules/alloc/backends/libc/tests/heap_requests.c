#include "heap_requests.h"

size_t heap_requests_malloc_size;
size_t heap_requests_realloc_size;

void *__wrap_malloc(size_t size)
{
    heap_requests_malloc_size = size;
    return __real_malloc(size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    heap_requests_realloc_size = size;
    return __real_realloc(ptr, size);
}
