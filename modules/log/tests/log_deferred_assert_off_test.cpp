/*
 * The deferred sink with assertions compiled out (elemental/log_deferred.h):
 * storage above 0 bytes and below one record is a contract violation, and
 * then the sink loses every record and writes nothing. That is what keeps a
 * swapped `storage_cap` and `message_max` from overrunning the storage.
 * With assertions on, the violation stops the program instead, which
 * log_deferred_violations_test.cpp shows; this file is built only without
 * them.
 */
#include <gtest/gtest.h>

#include <elemental/log.h>
#include <elemental/log_deferred.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "log_support.h"

namespace
{

constexpr size_t kMessageMax = 32;
constexpr uint8_t kGuard = 0xA5;

/* Static: a Zephyr image runs the tests on a 4 KiB stack. */
uint8_t storage[EL_LOG_DEFERRED_SINK_STORAGE_SIZE(3, kMessageMax)];

class ElLogDeferredAssertOff : public testing::Test
{
  protected:
    void SetUp() override
    {
        std::memset(storage, kGuard, sizeof(storage));
        recording_sink_init(&recording);
    }

    /* Logs a few records of every size through a sink set up as given, and
     * checks that each is lost, counted, and that no byte of the storage
     * was written. */
    void ExpectEveryRecordLostAndNothingWritten(size_t storage_cap, size_t message_max)
    {
        el_log_deferred_sink sink;
        el_log_deferred_sink_init(&sink, storage, storage_cap, message_max);
        const el_log_sink handle = el_log_deferred_sink_as_sink(&sink);

        el_log_record record;
        record.timestamp = 1;
        record.ticks_per_second = 1;
        record.level = EL_LOG_LEVEL_INFO;
        record.tag = "tag";
        const char *const messages[] = {"", "x", "a message of some length"};
        for (const char *message : messages) {
            record.message = message;
            record.len = std::strlen(message);
            EXPECT_EQ(el_log_sink_write(handle, &record), EL_ERR_NO_SPACE)
                << "\"" << message << "\"";
        }
        EXPECT_EQ(el_log_deferred_sink_lost(&sink), 3u);
        EXPECT_EQ(el_log_deferred_sink_pending_bytes(&sink), 0u);
        EXPECT_EQ(el_log_deferred_sink_drain(&sink, recording_sink_as_sink(&recording)), 0u);

        for (size_t i = 0; i < sizeof(storage); i++) {
            ASSERT_EQ(storage[i], kGuard) << "byte " << i << " of the storage was written";
        }
    }

    recording_sink recording;
};

TEST_F(ElLogDeferredAssertOff, StorageBetweenNothingAndOneRecordLosesEveryRecordAndWritesNothing)
{
    const size_t one = el_log_deferred_sink_record_size(kMessageMax);
    ExpectEveryRecordLostAndNothingWritten(1, kMessageMax);
    ExpectEveryRecordLostAndNothingWritten(one - 1, kMessageMax);
}

TEST_F(ElLogDeferredAssertOff, SwappedStorageSizeAndLongestMessageLosesEveryRecordAndWritesNothing)
{
    ExpectEveryRecordLostAndNothingWritten(kMessageMax, sizeof(storage));
}

} // namespace
