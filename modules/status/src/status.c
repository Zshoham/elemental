#include "elemental_config.h"

#include <elemental/status.h>

const char *el_status_name(el_status status)
{
    switch (status) {
    case EL_OK:
        return "ok";
    case EL_ERR_WOULD_BLOCK:
        return "would_block";
    case EL_ERR_END_OF_STREAM:
        return "end_of_stream";
    case EL_ERR_IO:
        return "io";
    case EL_ERR_NO_SPACE:
        return "no_space";
    default:
        return "unknown";
    }
}
