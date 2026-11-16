#ifndef SCRIPTED_CLOCK_H
#define SCRIPTED_CLOCK_H

#include <elemental/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A clock that answers what the test wrote into it, and records how it was
 * asked. It can be made to break the operation contract.
 */
typedef struct scripted_clock {
    el_ticks reading;
    uint32_t ticks_per_second;
    unsigned now_calls;
    unsigned ticks_per_second_calls;
    void *last_ctx;
} scripted_clock;

el_clock scripted_clock_as_clock(scripted_clock *clock);

/* Handles that were not made properly: no operations, or one missing. */
el_clock scripted_clock_without_now(scripted_clock *clock);
el_clock scripted_clock_without_ticks_per_second(scripted_clock *clock);

#ifdef __cplusplus
}
#endif

#endif /* SCRIPTED_CLOCK_H */
