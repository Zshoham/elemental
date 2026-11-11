/*
 * The assertion handler tests use when the configuration asks the
 * application to provide one: report the location and abort.
 */
#include <elemental/assert_handler.h>

#include <stdio.h>
#include <stdlib.h>

void el_assert_failed(const char *file, int line)
{
    fprintf(stderr, "%s:%d: Elemental contract assertion failed\n", file, line);
    abort();
}
