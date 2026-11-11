// C++ smoke test for Elemental's C contracts. C++11, compiled, linked, run.
//
// What it shows, beyond "the headers parse as C++":
//
//   - A C++ object implements an Elemental interface. The operation handed
//     to C is an extern "C" function that cannot throw; the exception it
//     catches is kept on the C++ side and rethrown after the C call returns.
//     Nothing unwinds through a C frame.
//   - Handles and status values are plain data: standard layout, trivially
//     copyable, fixed size.
//   - Caller-owned storage maps onto ordinary C++ lifetimes.

#include <elemental/io.h>
#include <elemental/io_mem.h>
#include <elemental/status.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>

static_assert(sizeof(el_status) == 4, "el_status is a 32-bit integer");
static_assert(std::is_standard_layout<el_reader>::value, "handles are plain data");
static_assert(std::is_trivially_copyable<el_writer>::value, "handles are passed by value");
static_assert(sizeof(el_writer) == 2 * sizeof(void *), "a handle is two pointers");

namespace
{

int failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            ++failures;                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                                      \
    } while (0)

// A writer that appends to a std::string, refusing to grow past a limit.
class StringSink
{
  public:
    explicit StringSink(std::size_t limit) : limit_(limit)
    {
    }

    // Valid while this object is alive and is not moved.
    el_writer writer() noexcept;

    const std::string &text() const noexcept
    {
        return text_;
    }

    // Rethrows what the last failed write threw, on the C++ side of the call.
    void rethrow_if_failed()
    {
        if (error_) {
            std::exception_ptr error = error_;
            error_ = nullptr;
            std::rethrow_exception(error);
        }
    }

    // May throw. Called only from the trampoline below.
    void append(const char *data, std::size_t len)
    {
        if (text_.size() + len > limit_) {
            throw std::length_error("StringSink limit exceeded");
        }
        text_.append(data, len);
    }

    void remember(std::exception_ptr error) noexcept
    {
        error_ = error;
    }

  private:
    std::size_t limit_;
    std::string text_;
    std::exception_ptr error_;
};

} // namespace

// The operation C calls. C linkage, and no exception leaves it.
extern "C" {
static el_status string_sink_write(void *ctx, const void *data, size_t len, size_t *n) noexcept
{
    StringSink *const sink = static_cast<StringSink *>(ctx);
    try {
        sink->append(static_cast<const char *>(data), len);
    } catch (...) {
        sink->remember(std::current_exception());
        return EL_ERR_IO;
    }
    *n = len;
    return EL_OK;
}
}

namespace
{

const el_writer_ops string_sink_ops = {string_sink_write};

el_writer StringSink::writer() noexcept
{
    const el_writer handle = {this, &string_sink_ops};
    return handle;
}

void copy_through_c_and_cxx()
{
    const char message[] = "elemental from C++";
    el_mem_reader source;
    el_mem_reader_init(&source, message, std::strlen(message));
    const el_reader reader = el_mem_reader_as_reader(&source);

    StringSink sink(64);
    const el_writer writer = sink.writer();

    // A C reader feeding a C++ writer, in small pieces.
    for (;;) {
        char chunk[5];
        size_t got = 0;
        const el_status status = el_read(reader, chunk, sizeof(chunk), &got);
        if (status == EL_ERR_END_OF_STREAM) {
            break;
        }
        CHECK(status == EL_OK);
        size_t put = 0;
        CHECK(el_write_all(writer, chunk, got, &put) == EL_OK);
        CHECK(put == got);
    }

    CHECK(sink.text() == message);
    CHECK(el_mem_reader_remaining(&source) == 0);
}

void exception_becomes_a_status_and_is_recovered()
{
    StringSink sink(4);
    size_t put = 99;

    // "abcd" fits, "efgh" does not: the second append throws inside the
    // operation. C sees a status; no exception crosses el_write_all().
    CHECK(el_write_all(sink.writer(), "abcd", 4, &put) == EL_OK);
    CHECK(el_write_all(sink.writer(), "efgh", 4, &put) == EL_ERR_IO);
    CHECK(put == 0);
    CHECK(sink.text() == "abcd");

    bool recovered = false;
    try {
        sink.rethrow_if_failed();
    } catch (const std::length_error &) {
        recovered = true;
    }
    CHECK(recovered);
}

} // namespace

int main()
{
    copy_through_c_and_cxx();
    exception_becomes_a_status_and_is_recovered();
    CHECK(std::strcmp(el_status_name(EL_ERR_WOULD_BLOCK), "would_block") == 0);

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
