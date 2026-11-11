/*
 * The entry point of the Zephyr test image. Every Elemental test registered
 * with elemental_add_test() is compiled into this image; GoogleTest runs
 * them all and prints the report Twister's gtest harness reads.
 */
#include <gtest/gtest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

#include <cstdio>

namespace
{

/*
 * Twister's gtest harness (Zephyr v4.5.0) knows a test ended from "[ OK ]",
 * "[ FAILED ]", or "[ DISABLED ]", but not from GoogleTest's "[ SKIPPED ]",
 * and fails the run when a test it saw start never ends. A contract test
 * skips when a fixture lacks a capability, so a skip is reported again in
 * the form the harness counts as skipped.
 */
class ReportSkipsToTwister : public testing::EmptyTestEventListener
{
    void OnTestEnd(const testing::TestInfo &test) override
    {
        if (test.result()->Skipped()) {
            std::printf("[ DISABLED ] %s.%s\n", test.test_suite_name(), test.name());
        }
    }
};

} // namespace

/* Zephyr's malloc (lib/libc/common), with CONFIG_SYS_HEAP_RUNTIME_STATS. */
extern "C" int malloc_runtime_stats_get(struct sys_memory_stats *stats);

int main(void)
{
    char program[] = "elemental_tests";
    char *argv[] = {program, nullptr};
    int argc = 1;
    testing::InitGoogleTest(&argc, argv);
    testing::UnitTest::GetInstance()->listeners().Append(new ReportSkipsToTwister);
    const int result = RUN_ALL_TESTS();

#ifndef CONFIG_ARCH_POSIX
    /*
     * The memory the run needed, recorded in docs/build.md: the peak heap
     * (GoogleTest allocates its registry and messages) and the stack, which
     * CONFIG_MAIN_STACK_SIZE must cover. Not meaningful on native_sim, whose
     * threads run on the host's stacks.
     */
    struct sys_memory_stats heap;
    if (malloc_runtime_stats_get(&heap) == 0) {
        std::printf("heap: %zu bytes at most\n", heap.max_allocated_bytes);
    }
    size_t unused = 0;
    if (k_thread_stack_space_get(k_current_get(), &unused) == 0) {
        std::printf("main stack: %zu of %d bytes used\n",
                    static_cast<size_t>(CONFIG_MAIN_STACK_SIZE) - unused, CONFIG_MAIN_STACK_SIZE);
    }
#endif
    return result;
}
