/*
 * EL_LAYOUT_OF takes a type, and C++ has no _Alignof, so the macro is
 * exercised from C. Each function returns EL_LAYOUT_OF of one type; the
 * unit test compares it with what C++ says about the same type.
 */
#ifndef ALLOC_LAYOUT_PROBE_H
#define ALLOC_LAYOUT_PROBE_H

#include <stdint.h>

#include <elemental/alloc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct alloc_probe_mixed {
    char tag;
    double value;
    uint16_t count;
} alloc_probe_mixed;

el_layout alloc_probe_char(void);
el_layout alloc_probe_u16(void);
el_layout alloc_probe_u64(void);
el_layout alloc_probe_double(void);
el_layout alloc_probe_pointer(void);
el_layout alloc_probe_struct(void);
el_layout alloc_probe_array(void);

#ifdef __cplusplus
}
#endif

#endif /* ALLOC_LAYOUT_PROBE_H */
