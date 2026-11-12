/*
 * The allocator contract as tests. Each test names one promise made in
 * elemental/alloc.h, or one capability a fixture claims. This file uses
 * nothing but the alloc module and GoogleTest.
 */
#include <elemental/alloc_contract.hpp>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace
{

constexpr size_t kCountMax = EL_ALLOC_CONTRACT_COUNT_MAX;
constexpr size_t kSizeMax = EL_ALLOC_CONTRACT_SIZE_MAX;
constexpr size_t kAlignMax = EL_ALLOC_CONTRACT_ALIGN_MAX;
constexpr size_t kLimit = EL_ALLOC_CONTRACT_LIMIT;

/* Cycles of allocating and freeing in the reclaim tests: far more than any
 * bounded allocator could satisfy without getting its storage back. */
constexpr int kCycles = 1000;

/* One live allocation, as the suite remembers it. */
struct Block {
    uint8_t *ptr;
    size_t size;
    size_t align;
    uint8_t seed;
};

bool IsAligned(const void *ptr, size_t align)
{
    return (reinterpret_cast<uintptr_t>(ptr) & (align - 1)) == 0;
}

/* A byte pattern that differs per block and per offset, so an overlap or a
 * shifted copy is visible. */
uint8_t PatternByte(uint8_t seed, size_t offset)
{
    return static_cast<uint8_t>(seed * 37u + offset * 11u + 1u);
}

void Fill(const Block &b)
{
    for (size_t i = 0; i < b.size; i++) {
        b.ptr[i] = PatternByte(b.seed, i);
    }
}

/* True if every byte still holds its pattern. Reports the first that does not. */
bool Intact(const Block &b)
{
    for (size_t i = 0; i < b.size; i++) {
        if (b.ptr[i] != PatternByte(b.seed, i)) {
            ADD_FAILURE() << "block " << static_cast<unsigned>(b.seed) << " (" << b.size
                          << " bytes, alignment " << b.align << ") changed at offset " << i;
            return false;
        }
    }
    return true;
}

/*
 * Wraps a handle and counts the calls that reach it. The suite uses this to
 * see which requests the front functions answer on their own.
 */
struct Counting {
    el_allocator inner;
    int allocs = 0;
    int frees = 0;

    static void *Alloc(void *ctx, el_layout layout)
    {
        Counting *const self = static_cast<Counting *>(ctx);
        self->allocs++;
        return el_alloc(self->inner, layout);
    }

    static void Free(void *ctx, void *ptr, el_layout layout)
    {
        Counting *const self = static_cast<Counting *>(ctx);
        self->frees++;
        el_free(self->inner, ptr, layout);
    }

    el_allocator Handle()
    {
        static const el_allocator_ops ops = {Alloc, Free};
        return el_allocator{this, &ops};
    }
};

/*
 * One test's view of the allocator under test, and every block it holds.
 * Blocks still held when a test ends are freed, so a failing test does not
 * leave the next one a full allocator.
 */
class Harness
{
  public:
    explicit Harness(const el_alloc_contract_fixture *fixture) : fx_(fixture)
    {
    }

    ~Harness()
    {
        FreeAll();
    }

    el_allocator Open()
    {
        FreeAll();
        fx_->reset(fx_->ctx);
        a_ = fx_->allocator(fx_->ctx);
        return a_;
    }

    el_allocator OpenBounded()
    {
        FreeAll();
        fx_->reset(fx_->ctx);
        a_ = fx_->allocator_bounded(fx_->ctx, kLimit);
        return a_;
    }

    /* Allocate and fill. Null if the allocator refused. */
    const Block *Take(size_t size, size_t align)
    {
        uint8_t *const ptr = static_cast<uint8_t *>(el_alloc(a_, el_layout_make(size, align)));
        if (ptr == nullptr) {
            return nullptr;
        }
        EXPECT_TRUE(IsAligned(ptr, align))
            << "a request for " << size << " bytes aligned to " << align << " got " << ptr;
        blocks_.push_back(Block{ptr, size, align, next_seed_++});
        Fill(blocks_.back());
        return &blocks_.back();
    }

    /* Free the block at `index`, after checking it is intact. */
    void Give(size_t index)
    {
        const Block b = blocks_[index];
        EXPECT_TRUE(Intact(b));
        blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(index));
        el_free(a_, b.ptr, el_layout_make(b.size, b.align));
    }

    bool AllIntact() const
    {
        bool intact = true;
        for (const Block &b : blocks_) {
            intact = Intact(b) && intact;
        }
        return intact;
    }

    void FreeAll()
    {
        while (!blocks_.empty()) {
            Give(blocks_.size() - 1);
        }
    }

    size_t Held() const
    {
        return blocks_.size();
    }

  private:
    const el_alloc_contract_fixture *fx_;
    el_allocator a_ = {nullptr, nullptr};
    std::vector<Block> blocks_;
    uint8_t next_seed_ = 1;
};

/* The working set: kCountMax blocks of varied sizes and alignments, whose
 * sizes total no more than kSizeMax. */
struct Request {
    size_t size;
    size_t align;
};

std::vector<Request> WorkingSet()
{
    std::vector<Request> requests;
    const size_t sizes[] = {1, 7, 16, 33, 64, 100, 3, 128};
    size_t total = 0;
    for (size_t i = 0; i < kCountMax; i++) {
        const size_t size = sizes[i % (sizeof(sizes) / sizeof(sizes[0]))];
        const size_t align = size_t{1} << (i % 7); /* 1 .. 64 */
        total += size;
        requests.push_back(Request{size, align});
    }
    EXPECT_LE(total, kSizeMax) << "the suite's working set is larger than it promises";
    return requests;
}

bool Claims(const el_alloc_contract_fixture *fx, uint32_t level)
{
    return fx->allocator_bounded != nullptr && fx->reclaims >= level;
}

} // namespace

/* A test may link this library only for the checking allocator, without
 * instantiating the suite. */
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ElAllocContract);

void ElAllocContract::SetUp()
{
    const el_alloc_contract_fixture *const fixture = GetParam();
    ASSERT_TRUE(fixture != nullptr) << "the suite was instantiated with a null fixture";
    ASSERT_TRUE(fixture->reset != nullptr) << "fixture.reset is required";
    ASSERT_TRUE(fixture->allocator != nullptr) << "fixture.allocator is required";
    ASSERT_LE(fixture->reclaims, uint32_t{EL_ALLOC_CONTRACT_RECLAIMS_ANY})
        << "fixture.reclaims is not one of EL_ALLOC_CONTRACT_RECLAIMS_*";
}

void ElAllocContract::TearDown()
{
    const el_alloc_contract_fixture *const fixture = GetParam();
    if (fixture != nullptr && fixture->reset != nullptr) {
        fixture->reset(fixture->ctx);
    }
}

void PrintTo(const el_alloc_contract_fixture *fixture, std::ostream *os)
{
    if (fixture == nullptr) {
        *os << "(null fixture)";
    } else if (fixture->name == nullptr) {
        *os << "(unnamed fixture)";
    } else {
        *os << fixture->name;
    }
}

/* ---- Every allocator ---------------------------------------------------- */

TEST_P(ElAllocContract, AllocationsAreAlignedAsRequested)
{
    Harness h(GetParam());
    h.Open();

    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        for (const size_t size : {size_t{1}, align, 3 * align + 1}) {
            SCOPED_TRACE(testing::Message() << size << " bytes aligned to " << align);
            ASSERT_NE(h.Take(size, align), nullptr);
            /* Take() checks the alignment. */
        }
        h.FreeAll();
    }
}

TEST_P(ElAllocContract, AllocationsAreWritableAndDoNotOverlap)
{
    Harness h(GetParam());
    h.Open();

    for (const Request &r : WorkingSet()) {
        ASSERT_NE(h.Take(r.size, r.align), nullptr)
            << "the working set did not fit: " << r.size << " bytes aligned to " << r.align;
    }
    /* Every block was filled after the previous one: an overlap shows. */
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, FreeAcceptsAnyOrderAndKeepsTheRestIntact)
{
    Harness h(GetParam());
    const std::vector<Request> set = WorkingSet();

    /* First in first out, last in first out, and every other block first. */
    for (int order = 0; order < 3; order++) {
        SCOPED_TRACE(testing::Message() << "order " << order);
        h.Open();
        for (const Request &r : set) {
            ASSERT_NE(h.Take(r.size, r.align), nullptr);
        }
        while (h.Held() > 0) {
            size_t index = 0;
            if (order == 1) {
                index = h.Held() - 1;
            } else if (order == 2) {
                index = h.Held() > 1 ? 1 : 0;
            }
            h.Give(index);
            ASSERT_TRUE(h.AllIntact());
        }
    }
}

TEST_P(ElAllocContract, AllocatingStillWorksAfterFreeing)
{
    Harness h(GetParam());
    h.Open();

    ASSERT_NE(h.Take(40, 8), nullptr);
    ASSERT_NE(h.Take(24, 16), nullptr);
    h.Give(0);
    ASSERT_NE(h.Take(16, 4), nullptr);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, ZeroSizeRequestsNeverReachTheAllocator)
{
    Harness h(GetParam());
    Counting counting{h.Open()};
    const el_allocator a = counting.Handle();

    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        const el_layout zero = el_layout_make(0, align);
        void *const ptr = el_alloc(a, zero);
        EXPECT_NE(ptr, nullptr) << "a zero-size request is not a failure";
        EXPECT_TRUE(IsAligned(ptr, align));
        el_free(a, ptr, zero);
        el_free(a, nullptr, zero);

        /* Zero-size arrays: no elements, or elements of no size. */
        el_layout none = el_layout_make(1, 1);
        ASSERT_EQ(el_layout_array(el_layout_make(8, align), 0, &none), EL_OK);
        void *const empty = el_alloc(a, none);
        EXPECT_NE(empty, nullptr);
        el_free(a, empty, none);
        ASSERT_EQ(el_layout_array(el_layout_make(0, align), 8, &none), EL_OK);
        EXPECT_NE(el_alloc(a, none), nullptr);
    }
    EXPECT_EQ(counting.allocs, 0);
    EXPECT_EQ(counting.frees, 0);
}

TEST_P(ElAllocContract, ArrayRequestsAllocateTheProduct)
{
    Harness h(GetParam());
    const el_allocator a = h.Open();

    el_layout layout;
    ASSERT_EQ(el_layout_array(el_layout_make(12, 4), 10, &layout), EL_OK);
    uint8_t *const bytes = static_cast<uint8_t *>(el_alloc(a, layout));
    ASSERT_NE(bytes, nullptr);
    EXPECT_TRUE(IsAligned(bytes, 4));
    for (size_t i = 0; i < 120; i++) {
        bytes[i] = static_cast<uint8_t>(i);
    }
    for (size_t i = 0; i < 120; i++) {
        ASSERT_EQ(bytes[i], static_cast<uint8_t>(i));
    }
    el_free(a, bytes, layout);
}

TEST_P(ElAllocContract, AnImpossibleRequestReturnsNull)
{
    Harness h(GetParam());
    const el_allocator a = h.Open();

    EXPECT_EQ(el_alloc(a, el_layout_make(SIZE_MAX, 1)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(SIZE_MAX, kAlignMax)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(SIZE_MAX - kAlignMax / 2, kAlignMax)), nullptr);
    /* And the allocator still works. */
    EXPECT_NE(h.Take(16, 8), nullptr);
}

/* ---- Bounded ------------------------------------------------------------ */

TEST_P(ElAllocContract, BoundedNeverHoldsMoreThanItsLimit)
{
    if (GetParam()->allocator_bounded == nullptr) {
        GTEST_SKIP() << "the fixture has no allocator_bounded: this allocator cannot be bounded";
    }
    Harness h(GetParam());
    h.OpenBounded();

    size_t taken = 0;
    while (taken <= kLimit && h.Take(1, 1) != nullptr) {
        taken++;
    }
    EXPECT_LE(taken, kLimit) << "a bounded allocator held more bytes than its limit";
    EXPECT_GE(taken, 1u);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, BoundedRefusesARequestLargerThanItsLimit)
{
    if (GetParam()->allocator_bounded == nullptr) {
        GTEST_SKIP() << "the fixture has no allocator_bounded: this allocator cannot be bounded";
    }
    Harness h(GetParam());
    const el_allocator a = h.OpenBounded();

    EXPECT_EQ(el_alloc(a, el_layout_make(kLimit + 1, 1)), nullptr);
    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        SCOPED_TRACE(testing::Message() << "alignment " << align);
        h.OpenBounded();
        EXPECT_NE(h.Take(64, align), nullptr) << "a fresh bounded allocator must satisfy 64 bytes";
    }
}

TEST_P(ElAllocContract, AFailedRequestChangesNothing)
{
    if (GetParam()->allocator_bounded == nullptr) {
        GTEST_SKIP() << "the fixture has no allocator_bounded: this allocator cannot be bounded";
    }
    Harness h(GetParam());
    const el_allocator a = h.OpenBounded();

    ASSERT_NE(h.Take(64, 8), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(kLimit, 1)), nullptr);
    EXPECT_EQ(el_alloc(a, el_layout_make(SIZE_MAX, 1)), nullptr);
    el_layout square;
    ASSERT_EQ(el_layout_array(el_layout_make(kLimit, 1), kLimit, &square), EL_OK);
    EXPECT_EQ(el_alloc(a, square), nullptr);
    /* What fitted before the failures still fits after them. */
    EXPECT_NE(h.Take(64, 8), nullptr);
    EXPECT_TRUE(h.AllIntact());
}

/* ---- Reclaiming --------------------------------------------------------- */

TEST_P(ElAllocContract, FreeingTheLatestAllocationGivesItsStorageBack)
{
    if (!Claims(GetParam(), EL_ALLOC_CONTRACT_RECLAIMS_LATEST)) {
        GTEST_SKIP() << "the fixture does not claim RECLAIMS_LATEST: freeing gives nothing back";
    }
    Harness h(GetParam());

    /* For each request, in a fresh allocator: a long-lived allocation
     * underneath, then the same request over and over, each freed before
     * the next. */
    for (const Request &r : {Request{100, 8}, Request{50, 16}, Request{64, 64}, Request{1, 1}}) {
        SCOPED_TRACE(testing::Message() << r.size << " bytes aligned to " << r.align);
        h.OpenBounded();
        ASSERT_NE(h.Take(32, 8), nullptr);
        for (int i = 0; i < kCycles; i++) {
            ASSERT_NE(h.Take(r.size, r.align), nullptr) << "cycle " << i;
            h.Give(h.Held() - 1);
        }
        EXPECT_TRUE(h.AllIntact());
    }
}

TEST_P(ElAllocContract, FreedStorageIsReusableInAnyOrder)
{
    if (!Claims(GetParam(), EL_ALLOC_CONTRACT_RECLAIMS_ANY)) {
        GTEST_SKIP()
            << "the fixture does not claim RECLAIMS_ANY: only some frees give storage back";
    }
    Harness h(GetParam());
    h.OpenBounded();

    const Request set[] = {{16, 8}, {40, 8}, {8, 4}, {30, 16}, {16, 8}};
    for (int i = 0; i < kCycles / 10; i++) {
        SCOPED_TRACE(testing::Message() << "cycle " << i);
        for (const Request &r : set) {
            ASSERT_NE(h.Take(r.size, r.align), nullptr);
        }
        /* Free from the front, then from the middle: never only the last. */
        h.Give(0);
        h.Give(h.Held() / 2);
        while (h.Held() > 0) {
            h.Give(0);
        }
    }
}
