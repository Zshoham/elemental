/*
 * Contract fixture for the writer sink. What it delivers is text, so the
 * fixture takes lines apart again:
 *
 *     [     1.500000] I contract: <message>
 *
 * The level's letter and the tag can be read back. The time cannot, not
 * exactly: the line holds seconds and microseconds, not ticks and their
 * frequency, so `time` is left null and the suite skips that test. The
 * line format is this sink's own promise and has its own tests
 * (log_sinks_test.cpp).
 */
#include "fixture_writer.h"

#include <elemental/io_buf.h>
#include <elemental/log_writer.h>

#include <string.h>

/* One line of the suite's longest record: "[ssssss.uuuuuu] L contract: ",
 * the message, a newline. */
#define PREFIX_LEN 16u
#define LINE_MAX_LEN \
    (PREFIX_LEN + 2u + (sizeof(EL_LOG_CONTRACT_TAG) - 1u) + 2u + EL_LOG_CONTRACT_MESSAGE_MAX + 1u)

static char text_storage[EL_LOG_CONTRACT_RECORDS_MAX * LINE_MAX_LEN];
static el_buf text;
static char line_storage[LINE_MAX_LEN + 8u];
static el_buf line_buf;
static el_log_writer_sink writer_sink;

static el_log_sink open_sink(size_t text_cap)
{
    el_buf_init(&text, text_storage, text_cap);
    el_buf_init(&line_buf, line_storage, sizeof(line_storage));
    el_log_writer_sink_init(&writer_sink, el_buf_as_writer(&text), &line_buf);
    return el_log_writer_sink_as_sink(&writer_sink);
}

static void reset(void *ctx)
{
    (void)ctx;
    el_buf_init(&text, NULL, 0);
}

static el_log_sink sink(void *ctx)
{
    (void)ctx;
    return open_sink(sizeof(text_storage));
}

static el_log_sink sink_full_after(void *ctx, size_t records)
{
    (void)ctx;
    /* The stream has room for exactly that many of the longest lines. The
     * next one is cut short by the stream, and has no newline. */
    return open_sink(records * LINE_MAX_LEN);
}

/* Lines that ended: a record is delivered once its newline is in. */
static size_t delivered(void *ctx)
{
    (void)ctx;
    const char *const data = (const char *)el_buf_data(&text);
    size_t lines = 0;
    for (size_t i = 0; i < el_buf_len(&text); i++) {
        if (data[i] == '\n') {
            lines++;
        }
    }
    return lines;
}

/* Line `index`, without its newline. */
static const char *line_at(size_t index, size_t *len)
{
    const char *const data = (const char *)el_buf_data(&text);
    size_t start = 0;
    for (size_t i = 0; i < el_buf_len(&text); i++) {
        if (data[i] != '\n') {
            continue;
        }
        if (index == 0) {
            *len = i - start;
            return data + start;
        }
        index--;
        start = i + 1;
    }
    *len = 0;
    return data;
}

/* Where the tag ends in a line: the first ": " after the level's letter. */
static size_t tag_end(const char *line, size_t len)
{
    for (size_t i = PREFIX_LEN + 2u; i + 1 < len; i++) {
        if (line[i] == ':' && line[i + 1] == ' ') {
            return i;
        }
    }
    return len;
}

static size_t message(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    size_t len;
    const char *const line = line_at(index, &len);
    const size_t end = tag_end(line, len);
    const size_t start = end + 2u <= len ? end + 2u : len;
    const size_t message_len = len - start;
    memcpy(out, line + start, message_len < cap ? message_len : cap);
    return message_len;
}

static el_log_level level(void *ctx, size_t index)
{
    (void)ctx;
    size_t len;
    const char *const line = line_at(index, &len);
    static const char letters[] = "DIWE";
    for (el_log_level i = 0; i < EL_LOG_LEVEL_OFF; i++) {
        if (len > PREFIX_LEN && line[PREFIX_LEN] == letters[i]) {
            return i;
        }
    }
    return EL_LOG_LEVEL_OFF; /* no record has it: the test fails */
}

static size_t tag(void *ctx, size_t index, char *out, size_t cap)
{
    (void)ctx;
    size_t len;
    const char *const line = line_at(index, &len);
    const size_t start = PREFIX_LEN + 2u;
    const size_t end = tag_end(line, len);
    const size_t tag_len = end > start ? end - start : 0;
    memcpy(out, line + start, tag_len < cap ? tag_len : cap);
    return tag_len;
}

const el_log_contract_fixture log_fixture_writer = {
    .name = "writer",
    .ctx = NULL,
    .reset = reset,
    .sink = sink,
    .delivered = delivered,
    .message = message,
    .level = level,
    .tag = tag,
    .time = NULL,
    .sink_full_after = sink_full_after,
};
