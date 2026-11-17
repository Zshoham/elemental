/* Test support for the tee sink: sinks that take no record. */
#ifndef REFUSING_SINK_H
#define REFUSING_SINK_H

#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A sink that refuses every record when it begins, with `status`:
 * EL_ERR_IO or EL_ERR_END_OF_STREAM. It has no state.
 */
el_log_sink refusing_sink(el_status status);

#ifdef __cplusplus
}
#endif

#endif /* REFUSING_SINK_H */
