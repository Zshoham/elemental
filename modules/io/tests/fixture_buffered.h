#ifndef FIXTURE_BUFFERED_H
#define FIXTURE_BUFFERED_H

#include <elemental/io_buffered.h>
#include <elemental/io_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* el_buffered_writer (elemental/io_buffered.h) under the contract suite, in
 * front of a sink that takes at most 3 bytes per call and stalls when full:
 * with 16 bytes of fixed storage; with grown storage over a 256-byte arena;
 * and with fixed storage of capacity 0, a pass-through writer, in front of
 * a sink that can also end. */
extern const el_io_contract_fixture io_fixture_buffered_fixed;
/* In fixture_buffered_grown.c, only with IO_ALLOC. */
extern const el_io_contract_fixture io_alloc_fixture_buffered_grown;
extern const el_io_contract_fixture io_fixture_buffered_pass_through;

/* ---- Shared by the fixtures, defined in fixture_buffered.c --------------- */

/* The inner writer: holds what it is given until taken, at most 3 bytes a
 * call; at `cap` it stalls, or with `ends`, has ended. */
typedef struct io_fixture_sink_state {
    uint8_t data[EL_IO_CONTRACT_SIZE_MAX];
    size_t len;
    size_t cap;
    int ends;
    size_t taken;
} io_fixture_sink_state;

extern io_fixture_sink_state io_fixture_sink;
extern el_buffered_writer io_fixture_buffered_writer;

void io_fixture_sink_open(size_t cap, int ends);
el_writer io_fixture_sink_writer(void);
/* Writes to `w` until a write fails, and returns it. */
el_writer io_fixture_fill_until_stalled(el_writer w);
/* Flushes io_fixture_buffered_writer, then takes from the sink. */
size_t io_fixture_buffered_take(void *ctx, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_BUFFERED_H */
