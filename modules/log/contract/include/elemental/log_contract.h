/*
 * elemental/log_contract.h - the fixture for the log sink contract suite.
 *
 * Every el_log_sink implementation is expected to pass the log sink
 * contract suite (elemental/log_contract.hpp). The suite checks what the
 * interface promises (elemental/log.h), not what a particular sink does
 * with a record; that belongs in the sink's own tests.
 *
 * A fixture tells the suite how to set up the sink under test and how to
 * see what it delivered: a sink's destination is its own business, so only
 * the fixture can look there. It is plain C, so a sink written in C is
 * described in C; only the line that hands the fixture to the suite is C++.
 *
 * See modules/log/tests/fixture_mem.c for the smallest complete fixture.
 */
#ifndef ELEMENTAL_LOG_CONTRACT_H
#define ELEMENTAL_LOG_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include <elemental/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The longest message the suite gives a sink, in bytes. */
#define EL_LOG_CONTRACT_MESSAGE_MAX 64u

/* The most records the suite gives one sink. */
#define EL_LOG_CONTRACT_RECORDS_MAX 8u

/*
 * The tag of every record the suite writes, and the longest timestamp in
 * seconds it uses where a fixture has no `time`. Fixed so that a sink which
 * turns records into text can be sized, and its text taken apart again: the
 * tag holds no colon, and the suite's messages are printable ASCII with no
 * newline.
 */
#define EL_LOG_CONTRACT_TAG "contract"
#define EL_LOG_CONTRACT_SECONDS_MAX 999999u

/*
 * How to set up the sink under test, and how to look at what it delivered.
 *
 * The suite uses one sink at a time. Before asking for a new one it calls
 * `reset`, which must release whatever the previous request created and
 * forget what was delivered. `ctx` is passed back to every function
 * unchanged. A record counts as delivered once the sink's end returned
 * EL_OK for it; `index` 0 is the first one delivered.
 */
typedef struct el_log_contract_fixture {
    /* Shown in failure reports. */
    const char *name;
    void *ctx;

    /* Required. Release the sink created by the previous request, if any. */
    void (*reset)(void *ctx);

    /*
     * Required. A sink that takes at least EL_LOG_CONTRACT_RECORDS_MAX
     * records of up to EL_LOG_CONTRACT_MESSAGE_MAX bytes each, and lends a
     * buffer that holds such a message whole.
     */
    el_log_sink (*sink)(void *ctx);

    /* Required. How many records the current sink has delivered. */
    size_t (*delivered)(void *ctx);

    /*
     * Required. Copy the message of delivered record `index` to `out`, at
     * most `cap` bytes, and return its whole length.
     */
    size_t (*message)(void *ctx, size_t index, char *out, size_t cap);

    /*
     * Optional capabilities: what else can be read back from where the sink
     * delivers. Leave one null if the destination does not hold it exactly;
     * the suite reports the corresponding test as skipped.
     */

    /* The level of delivered record `index`. */
    el_log_level (*level)(void *ctx, size_t index);

    /*
     * Copy the tag of delivered record `index` to `out`, at most `cap`
     * bytes, and return its whole length.
     */
    size_t (*tag)(void *ctx, size_t index, char *out, size_t cap);

    /* The timestamp and frequency of delivered record `index`, exactly. */
    void (*time)(void *ctx, size_t index, el_ticks *timestamp, uint32_t *ticks_per_second);

    /*
     * Optional capability. A sink that takes exactly `records` records
     * (at most EL_LOG_CONTRACT_RECORDS_MAX - 1) whose messages are
     * EL_LOG_CONTRACT_MESSAGE_MAX bytes long, and cannot take one more of
     * them. Leave null if the sink cannot be made to refuse a record.
     */
    el_log_sink (*sink_full_after)(void *ctx, size_t records);
} el_log_contract_fixture;

#ifdef __cplusplus
}
#endif

#endif /* ELEMENTAL_LOG_CONTRACT_H */
