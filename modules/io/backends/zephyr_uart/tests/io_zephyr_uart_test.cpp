/*
 * What only io_zephyr_uart promises (elemental/io_zephyr_uart.h), against
 * devices made by hand (fake_poll_uart.h): init reports a device that is
 * not ready, and a write passes every byte to the driver's poll_out, in
 * order. The writer over a real UART, with what it sent received on the
 * other end of the emulator's wire, is in
 * modules/ring/backends/zephyr_uart/tests.
 */
#include <gtest/gtest.h>

#include <elemental/io.h>
#include <elemental/io_zephyr_uart.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "fake_poll_uart.h"

namespace
{

std::string numbered(size_t first, size_t count)
{
    std::string bytes;
    for (size_t i = 0; i < count; i++) {
        bytes.push_back(static_cast<char>('A' + (first + i) % 26));
    }
    return bytes;
}

class IoZephyrUartPoll : public testing::Test
{
  protected:
    void SetUp() override
    {
        fake_poll_uart_reset();
    }

    el_zephyr_uart_poll sender{};
};

TEST_F(IoZephyrUartPoll, ADeviceThatIsNotReadyIsAnInitFailure)
{
    EXPECT_EQ(el_zephyr_uart_poll_init(&sender, nullptr), EL_ERR_IO);
    EXPECT_EQ(el_zephyr_uart_poll_init(&sender, fake_poll_uart_not_ready()), EL_ERR_IO);
    EXPECT_EQ(el_zephyr_uart_poll_init(&sender, fake_poll_uart()), EL_OK);
}

TEST_F(IoZephyrUartPoll, EveryByteIsPassedToThePollOutOfTheDeviceInOrder)
{
    ASSERT_EQ(el_zephyr_uart_poll_init(&sender, fake_poll_uart()), EL_OK);
    const el_writer out = el_zephyr_uart_poll_as_writer(&sender);
    const std::string first = numbered(0, 40);
    const std::string second = numbered(40, 3);

    /* Each write takes all it is given, and the bytes reach the driver one
     * by one, in the order written. */
    size_t n = 0;
    EXPECT_EQ(el_write(out, first.data(), first.size(), &n), EL_OK);
    EXPECT_EQ(n, first.size());
    EXPECT_EQ(el_write(out, second.data(), second.size(), &n), EL_OK);
    EXPECT_EQ(n, second.size());

    const std::string all = first + second;
    ASSERT_EQ(fake_poll_uart_sent_count(), all.size());
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(fake_poll_uart_sent()), all.size()), all);
}

} // namespace
