#include "layout_probe.h"

el_layout alloc_probe_char(void)
{
    return EL_LAYOUT_OF(char);
}

el_layout alloc_probe_u16(void)
{
    return EL_LAYOUT_OF(uint16_t);
}

el_layout alloc_probe_u64(void)
{
    return EL_LAYOUT_OF(uint64_t);
}

el_layout alloc_probe_double(void)
{
    return EL_LAYOUT_OF(double);
}

el_layout alloc_probe_pointer(void)
{
    return EL_LAYOUT_OF(void *);
}

el_layout alloc_probe_struct(void)
{
    return EL_LAYOUT_OF(alloc_probe_mixed);
}

el_layout alloc_probe_array(void)
{
    return EL_LAYOUT_OF(int32_t[3]);
}
