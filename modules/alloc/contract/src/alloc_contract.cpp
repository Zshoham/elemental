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
 * see which requests the front functions answer on their own, and which
 * operations they call.
 */
struct Counting {
    el_allocator inner;
    int allocs = 0;
    int frees = 0;
    int reallocs = 0;
    bool operation_refuses = false;

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
        static const el_allocator_ops ops = {Alloc, Free, nullptr};
        return el_allocator{this, &ops};
    }

    /* A handle whose realloc operation is counted, and which either answers
     * through the inner allocator's el_realloc() or, if `operation_refuses`,
     * returns null. */
    el_allocator HandleWithOperation()
    {
        static const el_allocator_ops ops = {Alloc, Free, Realloc};
        return el_allocator{this, &ops};
    }

    static void *Realloc(void *ctx, void *ptr, el_layout layout, size_t new_size)
    {
        Counting *const self = static_cast<Counting *>(ctx);
        self->reallocs++;
        EXPECT_NE(ptr, nullptr);
        EXPECT_NE(layout.size, size_t{0}) << "the operation was called for a size 0 layout";
        EXPECT_NE(new_size, size_t{0}) << "the operation was called with a new size of 0";
        EXPECT_NE(new_size, layout.size) << "the operation was called without a change";
        if (self->operation_refuses) {
            return nullptr;
        }
        return el_realloc(self->inner, ptr, layout, new_size);
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
        Reset();
        a_ = fx_->allocator(fx_->ctx);
        return a_;
    }

    el_allocator OpenBounded()
    {
        FreeAll();
        Reset();
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

    /* Take 1-byte blocks until the allocator refuses one; how many it gave.
     * A bounded allocator gives at most kLimit, so at most kLimit + 1 are
     * asked for. */
    size_t TakeUntilRefused()
    {
        size_t taken = 0;
        while (taken <= kLimit && Take(1, 1) != nullptr) {
            taken++;
        }
        return taken;
    }

    /* Route the requests that follow through `a`, a handle that wraps the
     * allocator Open() gave. It must outlive this harness. */
    void Use(el_allocator a)
    {
        a_ = a;
    }

    const Block &At(size_t index) const
    {
        return blocks_[index];
    }

    /*
     * Change the size of the block at `index` with el_realloc(), and
     * check the answer with Finish(). True if it was resized; false if the
     * allocator refused, in which case the block must be as it was.
     */
    bool Resize(size_t index, size_t new_size)
    {
        const Block b = blocks_[index];
        return Finish(index, el_realloc(a_, b.ptr, el_layout_make(b.size, b.align), new_size),
                      new_size);
    }

    /*
     * Check what a resize of the block at `index` to `new_size` answered
     * (`answer`) and adopt it. Null: the block is untouched, its bytes
     * whole. Otherwise: aligned, with the first min(old, new) bytes kept;
     * the block is then the answer, with the new size, filled afresh so
     * that any other block's overlap with it shows.
     */
    bool Finish(size_t index, void *answer, size_t new_size)
    {
        const Block old = blocks_[index];
        if (answer == nullptr) {
            EXPECT_TRUE(Intact(old)) << "a refused resize changed the block";
            return false;
        }
        Block now = old;
        now.ptr = static_cast<uint8_t *>(answer);
        now.size = new_size;
        EXPECT_TRUE(IsAligned(answer, old.align))
            << "a resize to " << new_size << " bytes aligned to " << old.align << " got " << answer;
        Block kept = now;
        kept.size = old.size < new_size ? old.size : new_size;
        EXPECT_TRUE(Intact(kept)) << "a resize from " << old.size << " to " << new_size
                                  << " bytes lost the bytes it had";
        blocks_[index] = now;
        Fill(now);
        return true;
    }

    /*
     * Ask for a resize of the block at `index` that must be refused. True
     * if it was, and the block is whole. On an answer that is not null the
     * resize was impossible, so neither the answer nor the old address can
     * be trusted: report it, and forget the block without touching or
     * freeing either.
     */
    bool RefusesResize(size_t index, size_t new_size)
    {
        const Block b = blocks_[index];
        void *const answer = el_realloc(a_, b.ptr, el_layout_make(b.size, b.align), new_size);
        if (answer == nullptr) {
            EXPECT_TRUE(Intact(b)) << "a refused resize changed the block";
            return true;
        }
        ADD_FAILURE() << "a resize from " << b.size << " to " << new_size
                      << " bytes that should have been refused returned " << answer;
        blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(index));
        return false;
    }

  private:
    void Reset() const
    {
        if (fx_->reset != nullptr) {
            fx_->reset(fx_->ctx);
        }
    }

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

/* ---- Changing size ------------------------------------------------------ */

/*
 * Sizes that every allocator in the repository can serve: the size classes
 * example's largest block is 256 bytes. kSmall to kLarge is a growth, and
 * back is a shrink.
 */
constexpr size_t kSmall = 24;
constexpr size_t kLarge = 120;

TEST_P(ElAllocContract, ReallocGrowsAnAllocationKeepingItsBytes)
{
    Harness h(GetParam());

    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        /* The allocation being grown is the latest, then not. */
        for (const bool latest : {true, false}) {
            SCOPED_TRACE(testing::Message()
                         << "alignment " << align << (latest ? ", latest" : ", with one after it"));
            h.Open();
            ASSERT_NE(h.Take(40, 8), nullptr);
            ASSERT_NE(h.Take(kSmall, align), nullptr);
            if (!latest) {
                ASSERT_NE(h.Take(33, 8), nullptr);
            }
            /* Resize() checks the alignment and that the first bytes were kept. */
            ASSERT_TRUE(h.Resize(1, kLarge)) << "the allocator has room for 120 bytes";
            /* What comes after must not overlap the grown allocation. */
            ASSERT_NE(h.Take(100, 8), nullptr);
            ASSERT_NE(h.Take(7, 1), nullptr);
            EXPECT_TRUE(h.AllIntact());
            /* Freed with its new layout, and everything else is still whole. */
            h.Give(1);
            ASSERT_NE(h.Take(100, 8), nullptr);
            EXPECT_TRUE(h.AllIntact());
        }
    }
}

TEST_P(ElAllocContract, ReallocShrinksAnAllocationKeepingItsFirstBytes)
{
    Harness h(GetParam());

    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        for (const bool latest : {true, false}) {
            SCOPED_TRACE(testing::Message()
                         << "alignment " << align << (latest ? ", latest" : ", with one after it"));
            h.Open();
            ASSERT_NE(h.Take(40, 8), nullptr);
            ASSERT_NE(h.Take(kLarge, align), nullptr);
            if (!latest) {
                ASSERT_NE(h.Take(33, 8), nullptr);
            }
            ASSERT_TRUE(h.Resize(1, kSmall)) << "an allocator with room can shrink";
            ASSERT_NE(h.Take(100, 8), nullptr);
            ASSERT_NE(h.Take(7, 1), nullptr);
            EXPECT_TRUE(h.AllIntact());
            h.Give(1);
            ASSERT_NE(h.Take(100, 8), nullptr);
            EXPECT_TRUE(h.AllIntact());
        }
    }
}

TEST_P(ElAllocContract, ReallocCanGrowAndShrinkTheSameAllocationRepeatedly)
{
    Harness h(GetParam());
    h.Open();

    ASSERT_NE(h.Take(16, 8), nullptr);
    ASSERT_NE(h.Take(16, 8), nullptr);
    for (const size_t size :
         {size_t{40}, size_t{100}, size_t{8}, size_t{64}, size_t{1}, size_t{50}}) {
        SCOPED_TRACE(testing::Message() << "to " << size << " bytes");
        ASSERT_TRUE(h.Resize(0, size));
        EXPECT_TRUE(h.AllIntact());
    }
}

TEST_P(ElAllocContract, ReallocToTheSameSizeReturnsThePointerWithoutAsking)
{
    Counting counting;
    Harness h(GetParam());
    counting.inner = h.Open();
    h.Use(counting.HandleWithOperation());

    const Block *const block = h.Take(kSmall, 8);
    ASSERT_NE(block, nullptr);
    uint8_t *const ptr = block->ptr;
    EXPECT_EQ(el_realloc(counting.HandleWithOperation(), ptr, el_layout_make(kSmall, 8), kSmall),
              ptr);
    EXPECT_EQ(counting.allocs, 1) << "only the allocation that made the block";
    EXPECT_EQ(counting.frees, 0);
    EXPECT_EQ(counting.reallocs, 0);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, ReallocOfNothingIsAnAllocationAndToNothingIsAFree)
{
    Counting counting;
    Harness h(GetParam());
    counting.inner = h.Open();
    const el_allocator a = counting.HandleWithOperation();

    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        SCOPED_TRACE(testing::Message() << "alignment " << align);
        const el_layout zero = el_layout_make(0, align);
        const el_layout some = el_layout_make(48, align);
        counting.allocs = counting.frees = counting.reallocs = 0;

        /* From a null pointer, and from the zero-size pointer. */
        void *const zero_pointer = el_alloc(a, zero);
        for (void *const nothing : {static_cast<void *>(nullptr), zero_pointer}) {
            uint8_t *const grown = static_cast<uint8_t *>(el_realloc(a, nothing, zero, 48));
            ASSERT_NE(grown, nullptr);
            EXPECT_TRUE(IsAligned(grown, align));
            for (size_t i = 0; i < 48; i++) {
                grown[i] = static_cast<uint8_t>(i);
            }
            /* To size 0: the allocation is freed, whatever was in it. */
            void *const gone = el_realloc(a, grown, some, 0);
            EXPECT_EQ(gone, zero_pointer) << "the zero-size pointer";
            EXPECT_NE(gone, nullptr);
            el_free(a, gone, zero);
        }
        /* From nothing to nothing is not a failure. */
        EXPECT_EQ(el_realloc(a, nullptr, zero, 0), zero_pointer);

        EXPECT_EQ(counting.allocs, 2) << "one for each growth from nothing";
        EXPECT_EQ(counting.frees, 2) << "one for each shrink to nothing";
        EXPECT_EQ(counting.reallocs, 0) << "no operation is called with a size of 0";
    }
}

TEST_P(ElAllocContract, ReallocWithoutTheOperationAllocatesCopiesAndFrees)
{
    Counting counting;
    Harness h(GetParam());
    counting.inner = h.Open();
    h.Use(counting.Handle());

    ASSERT_NE(h.Take(40, 8), nullptr);
    ASSERT_NE(h.Take(kSmall, 16), nullptr);
    ASSERT_TRUE(h.Resize(1, kLarge));
    EXPECT_EQ(counting.allocs, 3);
    EXPECT_EQ(counting.frees, 1);
    ASSERT_TRUE(h.Resize(1, kSmall));
    EXPECT_EQ(counting.allocs, 4);
    EXPECT_EQ(counting.frees, 2);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, ReallocAllocatesCopiesAndFreesWhenTheOperationReturnsNull)
{
    Counting counting;
    Harness h(GetParam());
    counting.inner = h.Open();
    counting.operation_refuses = true;
    h.Use(counting.HandleWithOperation());

    ASSERT_NE(h.Take(40, 8), nullptr);
    ASSERT_NE(h.Take(kSmall, 16), nullptr);
    ASSERT_TRUE(h.Resize(1, kLarge));
    EXPECT_EQ(counting.reallocs, 1) << "the operation is asked first";
    EXPECT_EQ(counting.allocs, 3);
    EXPECT_EQ(counting.frees, 1);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, ReallocTakesTheOperationsAnswerWhenItGivesOne)
{
    Counting counting;
    Harness h(GetParam());
    counting.inner = h.Open();
    h.Use(counting.HandleWithOperation());

    ASSERT_NE(h.Take(40, 8), nullptr);
    ASSERT_NE(h.Take(kSmall, 16), nullptr);
    ASSERT_TRUE(h.Resize(1, kLarge));
    /* The operation (here, the inner allocator's own resize) answered:
     * nothing more was allocated or freed through this handle. */
    EXPECT_EQ(counting.reallocs, 1);
    EXPECT_EQ(counting.allocs, 2);
    EXPECT_EQ(counting.frees, 0);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, AFailedReallocChangesNothing)
{
    Harness h(GetParam());
    h.Open();

    ASSERT_NE(h.Take(40, 8), nullptr);
    ASSERT_NE(h.Take(kSmall, 16), nullptr);
    ASSERT_NE(h.Take(33, 1), nullptr);
    for (const size_t index : {size_t{0}, size_t{1}, size_t{2}}) {
        SCOPED_TRACE(testing::Message() << "allocation " << index);
        ASSERT_TRUE(h.RefusesResize(index, SIZE_MAX));
        ASSERT_TRUE(h.RefusesResize(index, SIZE_MAX - kAlignMax));
    }
    /* Still at the same address, still freed with the same layout, and
     * its storage has not been given to anyone else. */
    ASSERT_NE(h.Take(100, 8), nullptr);
    ASSERT_NE(h.Take(60, 8), nullptr);
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, AFailedReallocInABoundedAllocatorChangesNothing)
{
    if (GetParam()->allocator_bounded == nullptr) {
        GTEST_SKIP() << "the fixture has no allocator_bounded: this allocator cannot be bounded";
    }
    Harness h(GetParam());

    /* What a fresh allocator holding one 64-byte block still gives, in
     * 1-byte requests: the control. */
    h.OpenBounded();
    ASSERT_NE(h.Take(64, 8), nullptr);
    const size_t spare = h.TakeUntilRefused();

    /* The same, after a resize past the limit was refused. */
    h.OpenBounded();
    ASSERT_NE(h.Take(64, 8), nullptr);
    ASSERT_TRUE(h.RefusesResize(0, kLimit + 1)) << "more than the limit";
    EXPECT_TRUE(h.AllIntact());
    EXPECT_EQ(h.TakeUntilRefused(), spare) << "the refused resize kept or gave away storage";
    EXPECT_TRUE(h.AllIntact());
}

TEST_P(ElAllocContract, BoundedNeverHoldsMoreThanItsLimitWhenAnAllocationGrows)
{
    if (GetParam()->allocator_bounded == nullptr) {
        GTEST_SKIP() << "the fixture has no allocator_bounded: this allocator cannot be bounded";
    }
    Harness h(GetParam());

    for (const bool latest : {true, false}) {
        SCOPED_TRACE(latest ? "latest" : "with one after it");
        h.OpenBounded();
        ASSERT_NE(h.Take(64, 8), nullptr);
        /* Every byte held counts against the limit, the other block's too. */
        const size_t others = latest ? 0 : 16;
        if (!latest) {
            ASSERT_NE(h.Take(others, 8), nullptr);
        }
        size_t size = 64;
        while (size + others <= kLimit && h.Resize(0, size + 16)) {
            size += 16;
        }
        EXPECT_LE(size + others, kLimit) << "the allocations grew past the bound";
        EXPECT_TRUE(h.AllIntact());
    }
}

TEST_P(ElAllocContract, TheReallocOperationKeepsTheBytesAndTheAlignmentWhenItMoves)
{
    Harness h(GetParam());
    if (h.Open().ops->realloc == nullptr) {
        GTEST_SKIP() << "the allocator has no realloc operation: el_realloc allocates, copies and "
                        "frees for it";
    }

    for (size_t align = 1; align <= kAlignMax; align *= 2) {
        SCOPED_TRACE(testing::Message() << "alignment " << align);
        const el_allocator a = h.Open();
        ASSERT_NE(h.Take(40, 8), nullptr);
        ASSERT_NE(h.Take(kSmall, align), nullptr);
        ASSERT_NE(h.Take(33, 1), nullptr);

        /* Grown, shrunk, grown: where the operation answers, Finish()
         * checks the alignment and the bytes; where it returns null, that
         * nothing changed. */
        for (const size_t size : {kLarge, size_t{9}, kLarge}) {
            const Block block = h.At(1);
            /* A refusal leaves the block at its size: the operation is
             * never asked for no change. */
            if (block.size != size) {
                void *const answer =
                    a.ops->realloc(a.ctx, block.ptr, el_layout_make(block.size, align), size);
                h.Finish(1, answer, size);
            }
            ASSERT_NE(h.Take(7, 1), nullptr);
            EXPECT_TRUE(h.AllIntact());
        }
    }
}

TEST_P(ElAllocContract, TheReallocOperationReturningNullChangesNothing)
{
    Harness h(GetParam());
    const el_allocator a = h.Open();
    if (a.ops->realloc == nullptr) {
        GTEST_SKIP() << "the allocator has no realloc operation: el_realloc allocates, copies and "
                        "frees for it";
    }

    ASSERT_NE(h.Take(40, 8), nullptr);
    ASSERT_NE(h.Take(kSmall, 8), nullptr);
    for (const size_t index : {size_t{0}, size_t{1}}) {
        SCOPED_TRACE(testing::Message() << "allocation " << index);
        const Block block = h.At(index);
        /* No allocator can make this one. */
        EXPECT_EQ(a.ops->realloc(a.ctx, block.ptr, el_layout_make(block.size, 8), SIZE_MAX),
                  nullptr);
        EXPECT_TRUE(h.AllIntact());
    }
    ASSERT_NE(h.Take(100, 8), nullptr);
    EXPECT_TRUE(h.AllIntact());
}
