/*
 * The log sink contract as tests. Each test names one promise made in
 * elemental/log.h, or one capability a fixture claims. This file uses
 * nothing but the log module and GoogleTest.
 */
#include <elemental/log_contract.hpp>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

namespace
{

constexpr size_t kMessageMax = EL_LOG_CONTRACT_MESSAGE_MAX;
constexpr size_t kRecordsMax = EL_LOG_CONTRACT_RECORDS_MAX;

/*
 * A message of `len` printable bytes that differs per `seed` and per
 * offset, so a dropped, repeated, reordered, or foreign byte is visible.
 * No newline and no colon: a sink that makes text of it can be read back.
 */
std::string Message(size_t len, unsigned seed)
{
    std::string text(len, ' ');
    for (size_t i = 0; i < len; i++) {
        /* 'A' to 'Z', 'a' to 'z': 52 values. */
        const unsigned value = (seed * 7u + static_cast<unsigned>(i) * 11u + 3u) % 52u;
        text[i] = static_cast<char>(value < 26u ? 'A' + value : 'a' + (value - 26u));
    }
    return text;
}

/* A record as the suite writes one: its own tag, an early time. */
el_log_record Record(const char *message, size_t len)
{
    el_log_record record;
    record.timestamp = 1500;
    record.ticks_per_second = 1000;
    record.level = EL_LOG_LEVEL_INFO;
    record.tag = EL_LOG_CONTRACT_TAG;
    record.message = message;
    record.len = len;
    return record;
}

/* One test's view of the sink under test. */
class Harness
{
  public:
    explicit Harness(const el_log_contract_fixture *fixture) : fx_(fixture)
    {
    }

    el_log_sink Open()
    {
        fx_->reset(fx_->ctx);
        return fx_->sink(fx_->ctx);
    }

    el_log_sink OpenFullAfter(size_t records)
    {
        fx_->reset(fx_->ctx);
        return fx_->sink_full_after(fx_->ctx, records);
    }

    size_t Delivered() const
    {
        return fx_->delivered(fx_->ctx);
    }

    /* The message of a delivered record. Larger than any the suite writes,
     * so a sink that delivers too much is caught, not truncated. */
    std::string DeliveredMessage(size_t index) const
    {
        char out[kMessageMax + 32];
        const size_t len = fx_->message(fx_->ctx, index, out, sizeof(out));
        EXPECT_LE(len, sizeof(out)) << "record " << index << " came back longer than any written";
        return std::string(out, len < sizeof(out) ? len : sizeof(out));
    }

    std::string DeliveredTag(size_t index) const
    {
        char out[64];
        const size_t len = fx_->tag(fx_->ctx, index, out, sizeof(out));
        EXPECT_LE(len, sizeof(out));
        return std::string(out, len < sizeof(out) ? len : sizeof(out));
    }

  private:
    const el_log_contract_fixture *fx_;
};

} // namespace

void ElLogContract::SetUp()
{
    const el_log_contract_fixture *const fixture = GetParam();
    ASSERT_TRUE(fixture != nullptr) << "the suite was instantiated with a null fixture";
    ASSERT_TRUE(fixture->reset != nullptr) << "fixture.reset is required";
    ASSERT_TRUE(fixture->sink != nullptr) << "fixture.sink is required";
    ASSERT_TRUE(fixture->delivered != nullptr) << "fixture.delivered is required";
    ASSERT_TRUE(fixture->message != nullptr) << "fixture.message is required";
}

void ElLogContract::TearDown()
{
    const el_log_contract_fixture *const fixture = GetParam();
    if (fixture != nullptr && fixture->reset != nullptr) {
        fixture->reset(fixture->ctx);
    }
}

void PrintTo(const el_log_contract_fixture *fixture, std::ostream *os)
{
    if (fixture == nullptr) {
        *os << "(null fixture)";
    } else if (fixture->name == nullptr) {
        *os << "(unnamed fixture)";
    } else {
        *os << fixture->name;
    }
}

TEST_P(ElLogContract, ANewSinkHasDeliveredNothing)
{
    Harness h(GetParam());
    (void)h.Open();

    EXPECT_EQ(h.Delivered(), 0u);
}

TEST_P(ElLogContract, DeliversAMessageByteForByte)
{
    Harness h(GetParam());

    for (const size_t len : {size_t{1}, size_t{2}, size_t{17}, kMessageMax - 1, kMessageMax}) {
        const el_log_sink sink = h.Open();
        const std::string message = Message(len, static_cast<unsigned>(len));
        const el_log_record record = Record(message.data(), message.size());

        ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK) << len << " bytes";
        ASSERT_EQ(h.Delivered(), 1u) << len << " bytes";
        EXPECT_EQ(h.DeliveredMessage(0), message) << len << " bytes";
    }
}

TEST_P(ElLogContract, DeliversRecordsInTheOrderWritten)
{
    Harness h(GetParam());
    const el_log_sink sink = h.Open();

    for (size_t i = 0; i < kRecordsMax; i++) {
        /* Lengths that differ too, so that one record's bytes cannot pass
         * for another's. */
        const std::string message = Message(kMessageMax - i * 5, static_cast<unsigned>(i));
        const el_log_record record = Record(message.data(), message.size());
        ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK) << "record " << i;
        ASSERT_EQ(h.Delivered(), i + 1) << "after record " << i;
    }

    for (size_t i = 0; i < kRecordsMax; i++) {
        EXPECT_EQ(h.DeliveredMessage(i), Message(kMessageMax - i * 5, static_cast<unsigned>(i)))
            << "record " << i;
    }
}

TEST_P(ElLogContract, CopiesTheMessageItKeeps)
{
    Harness h(GetParam());
    const el_log_sink sink = h.Open();

    /* One buffer for every record, as a caller that formats on its stack
     * has. What the sink kept of the first must not change with it. */
    char buffer[kMessageMax];
    const std::string first = Message(kMessageMax, 1);
    const std::string second = Message(kMessageMax, 2);

    first.copy(buffer, kMessageMax);
    el_log_record record = Record(buffer, kMessageMax);
    ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK);

    second.copy(buffer, kMessageMax);
    record = Record(buffer, kMessageMax);
    ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK);

    for (char &byte : buffer) {
        byte = '#';
    }

    ASSERT_EQ(h.Delivered(), 2u);
    EXPECT_EQ(h.DeliveredMessage(0), first);
    EXPECT_EQ(h.DeliveredMessage(1), second);
}

TEST_P(ElLogContract, AnEmptyMessageIsARecord)
{
    Harness h(GetParam());
    const el_log_sink sink = h.Open();

    /* Null with a length of 0 is how "nothing" is passed. */
    const el_log_record empty = Record(nullptr, 0);
    ASSERT_EQ(el_log_sink_write(sink, &empty), EL_OK);
    const std::string after = Message(3, 9);
    const el_log_record record = Record(after.data(), after.size());
    ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK);

    ASSERT_EQ(h.Delivered(), 2u);
    EXPECT_EQ(h.DeliveredMessage(0), "");
    EXPECT_EQ(h.DeliveredMessage(1), after);
}

TEST_P(ElLogContract, DeliversTheLevel)
{
    const el_log_contract_fixture *const fixture = GetParam();
    if (fixture->level == nullptr) {
        GTEST_SKIP() << "the fixture has no level: what this sink delivers does not hold it";
    }
    Harness h(fixture);
    const el_log_sink sink = h.Open();

    const el_log_level levels[] = {EL_LOG_LEVEL_ERROR, EL_LOG_LEVEL_DEBUG, EL_LOG_LEVEL_WARN,
                                   EL_LOG_LEVEL_INFO};
    const std::string message = Message(8, 4);
    for (const el_log_level level : levels) {
        el_log_record record = Record(message.data(), message.size());
        record.level = level;
        ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK) << el_log_level_name(level);
    }

    ASSERT_EQ(h.Delivered(), 4u);
    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(fixture->level(fixture->ctx, i), levels[i]) << "record " << i;
    }
}

TEST_P(ElLogContract, DeliversTheTag)
{
    const el_log_contract_fixture *const fixture = GetParam();
    if (fixture->tag == nullptr) {
        GTEST_SKIP() << "the fixture has no tag: what this sink delivers does not hold it";
    }
    Harness h(fixture);
    const el_log_sink sink = h.Open();

    const std::string message = Message(8, 5);
    const el_log_record record = Record(message.data(), message.size());
    ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK);

    ASSERT_EQ(h.Delivered(), 1u);
    EXPECT_EQ(h.DeliveredTag(0), EL_LOG_CONTRACT_TAG);
    EXPECT_EQ(h.DeliveredMessage(0), message);
}

TEST_P(ElLogContract, DeliversTheTimestampAndItsFrequency)
{
    const el_log_contract_fixture *const fixture = GetParam();
    if (fixture->time == nullptr) {
        GTEST_SKIP() << "the fixture has no time: what this sink delivers does not hold it exactly";
    }
    Harness h(fixture);
    const el_log_sink sink = h.Open();

    /* The frequency travels with the reading: two records of one sink may
     * come from clocks that count differently. */
    const struct {
        el_ticks timestamp;
        uint32_t ticks_per_second;
    } times[] = {{0, 1}, {12034, 1000}, {7509, 10000}, {UINT64_MAX, UINT32_MAX}};
    const std::string message = Message(8, 6);
    for (const auto &time : times) {
        el_log_record record = Record(message.data(), message.size());
        record.timestamp = time.timestamp;
        record.ticks_per_second = time.ticks_per_second;
        ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK);
    }

    ASSERT_EQ(h.Delivered(), 4u);
    for (size_t i = 0; i < 4; i++) {
        el_ticks timestamp = 77;
        uint32_t ticks_per_second = 77;
        fixture->time(fixture->ctx, i, &timestamp, &ticks_per_second);
        EXPECT_EQ(timestamp, times[i].timestamp) << "record " << i;
        EXPECT_EQ(ticks_per_second, times[i].ticks_per_second) << "record " << i;
    }
}

TEST_P(ElLogContract, RefusesARecordItCannotTakeAndKeepsTheOnesItTook)
{
    const el_log_contract_fixture *const fixture = GetParam();
    if (fixture->sink_full_after == nullptr) {
        GTEST_SKIP() << "the fixture has no sink_full_after: this sink cannot be made to refuse";
    }
    Harness h(fixture);

    for (const size_t room : {size_t{0}, size_t{1}, size_t{3}}) {
        const el_log_sink sink = h.OpenFullAfter(room);
        for (size_t i = 0; i < room; i++) {
            const std::string message = Message(kMessageMax, static_cast<unsigned>(i));
            const el_log_record record = Record(message.data(), message.size());
            ASSERT_EQ(el_log_sink_write(sink, &record), EL_OK)
                << "record " << i << " of a sink with room for " << room;
        }

        const std::string extra = Message(kMessageMax, 40);
        const el_log_record record = Record(extra.data(), extra.size());
        EXPECT_NE(el_log_sink_write(sink, &record), EL_OK)
            << "a sink with room for " << room << " took one more";

        /* Lost means lost: it is not delivered, whole or in part, and what
         * was delivered before it is as it was. */
        ASSERT_EQ(h.Delivered(), room) << "room for " << room;
        for (size_t i = 0; i < room; i++) {
            EXPECT_EQ(h.DeliveredMessage(i), Message(kMessageMax, static_cast<unsigned>(i)))
                << "record " << i << " of a sink with room for " << room;
        }
    }
}
