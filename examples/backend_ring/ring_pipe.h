/*
 * ring_pipe - an example io backend: a byte pipe over a caller-provided ring
 * buffer.
 *
 * This is what a backend author writes: a state struct, one operation per
 * interface, and a function that hands out a handle. It is the software
 * shape of a UART with a transmit or receive FIFO, which is why it is a
 * useful example: both ends can stall.
 *
 *   writer side: copies bytes in. Full -> EL_ERR_WOULD_BLOCK.
 *                After ring_pipe_close_write() -> EL_ERR_END_OF_STREAM.
 *   reader side: copies bytes out. Empty -> EL_ERR_WOULD_BLOCK, or
 *                EL_ERR_END_OF_STREAM once the write side is closed.
 *
 * Storage: the caller provides the ring_pipe and its byte storage and keeps
 * both alive while the pipe or a handle made from it is in use.
 *
 * Concurrency: none. This example is not safe to share between a thread and
 * an interrupt handler; a real FIFO would make `head` and `count` updates
 * atomic for its platform, which is exactly the part a port owns.
 *
 * Not part of Elemental's API: an example, named without the el_ prefix.
 */
#ifndef RING_PIPE_H
#define RING_PIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <elemental/io.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ring_pipe {
    uint8_t *storage;
    size_t capacity;
    size_t head;  /* index of the oldest byte */
    size_t count; /* bytes currently held */
    bool write_closed;
} ring_pipe;

/* `storage` may be null only if `capacity` is 0. */
void ring_pipe_init(ring_pipe *pipe, uint8_t *storage, size_t capacity);

/* No more bytes will be written. Readers drain what is held, then end. */
void ring_pipe_close_write(ring_pipe *pipe);

el_reader ring_pipe_as_reader(ring_pipe *pipe);
el_writer ring_pipe_as_writer(ring_pipe *pipe);

#ifdef __cplusplus
}
#endif

#endif /* RING_PIPE_H */
