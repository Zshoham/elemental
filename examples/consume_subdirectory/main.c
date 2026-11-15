/*
 * Consumer example: build a message in a fixed buffer, then parse it back.
 *
 * write_record() and read_record() know nothing about memory. They take a
 * writer and a reader, so the same functions would frame records onto a UART
 * or a socket. Only main() decides that the bytes live in a static buffer.
 */
#include <elemental/io.h>
#include <elemental/io_buf.h>
#include <elemental/io_mem.h>
#include <elemental/status.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A record is one length byte followed by that many bytes of text. */

/* The application's own failure. Negative statuses are the application's:
 * Elemental defines none, so this cannot collide with a code added later. */
enum { RECORD_ERR_TOO_LONG = -1 };

EL_NODISCARD static el_status write_record(el_writer out, const char *text)
{
    const size_t len = strlen(text);
    EL_ENSURE(len <= UINT8_MAX, RECORD_ERR_TOO_LONG);

    const uint8_t header = (uint8_t)len;
    EL_TRY(el_write_all(out, &header, 1, NULL));
    return el_write_all(out, text, len, NULL);
}

EL_NODISCARD static el_status read_record(el_reader in, char *text, size_t text_cap)
{
    uint8_t len;
    /* EL_ERR_END_OF_STREAM here means "no more records". */
    EL_TRY(el_read_exact(in, &len, 1, NULL));
    EL_ENSURE((size_t)len + 1 <= text_cap, EL_ERR_NO_SPACE);

    /* Any failure here is a record cut short. */
    EL_TRY(el_read_exact(in, text, len, NULL));
    text[len] = '\0';
    return EL_OK;
}

int main(void)
{
    /* Storage is the application's: a fixed buffer, no heap anywhere. */
    static uint8_t frame[32];
    el_buf message;
    el_buf_init(&message, frame, sizeof(frame));
    const el_writer out = el_buf_as_writer(&message);

    if (write_record(out, "hello") != EL_OK || write_record(out, "elemental") != EL_OK) {
        fprintf(stderr, "unexpected: the first two records fit\n");
        return 1;
    }

    /* The third record does not fit. The buffer says so; nothing overflows. */
    const el_status full = write_record(out, "this record is too long for the frame");
    printf("third record: %s after %zu of %zu bytes\n", el_status_name(full), el_buf_len(&message),
           sizeof(frame));
    if (full != EL_ERR_NO_SPACE) {
        return 1;
    }

    /* Read back what a receiver would get: two records, then a torn one. */
    el_mem_reader reader;
    el_mem_reader_init(&reader, el_buf_data(&message), el_buf_len(&message));
    const el_reader in = el_mem_reader_as_reader(&reader);

    char text[64];
    int records = 0;
    el_status status;
    while ((status = read_record(in, text, sizeof(text))) == EL_OK) {
        printf("record %d: %s\n", ++records, text);
    }
    printf("stopped: %s\n", el_status_name(status));

    return records == 2 && status == EL_ERR_END_OF_STREAM ? 0 : 1;
}
