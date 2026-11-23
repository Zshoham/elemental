# Changelog

Every change to a public interface is listed here, whatever its stability
level. See [docs/process/api-stability.md](docs/process/api-stability.md).

## 0.1.0 — unreleased

First milestone: the project's foundation.

- Licensed under Apache-2.0.
- `configs/assert_application_defconfig` is now `configs/variant_defconfig`,
  which also selects 256-entry CRC tables; `configs/assert_off_defconfig` and
  the `assert-off` preset are new.
- `status`: `EL_ERR_NO_SPACE` (4), the one capacity status
  ([ADR 0009](docs/adr/0009-capacity-status.md)).
- `status` (experimental): `el_status`, four codes, `el_status_name()`,
  `EL_ASSERT()`, and the application assertion handler.
- `io` (experimental): `el_reader` and `el_writer`, `el_read`,
  `el_write`, `el_read_exact`, `el_write_all`, memory streams, and the
  io contract suite.
- `io_posix` (experimental): reader and writer over a file descriptor.
- `alloc` (experimental): `el_allocator`, `el_alloc`, and `el_free`, which
  takes back the layout the allocation was made with
  ([ADR 0012](docs/adr/0012-sized-free.md)); `el_layout`, `el_layout_make`,
  `EL_LAYOUT_OF`, and `el_layout_array` describe an allocation's size and
  alignment as one value, in place of `(size, align)` pairs and of
  `el_alloc_array`, which is removed
  ([ADR 0013](docs/adr/0013-allocation-layout.md)); `el_bump_arena` and
  `el_stack_arena` over caller storage; the allocator contract suite
  `ElAllocContract` and the checking allocator `el_alloc_checker`
  (`elemental/alloc_contract.h`, `.hpp`).
- `alloc_libc` (experimental): `el_libc_allocator()`, over `malloc`,
  `aligned_alloc`, and `free`.
- `io` (experimental): `el_grow_writer` (`elemental/io_alloc.h`), a writer
  that grows through an allocator. Part of `elemental::io` when the Kconfig
  symbol `IO_ALLOC` is on (the default), which makes `elemental::io` link
  `elemental::alloc`. With `IO_ALLOC` off, io builds and links no allocator.
- `fmt` (experimental): `el_fmt`, a cursor over caller memory, and
  `el_fmt_str`, `el_fmt_cstr`, `el_fmt_dec_u64`, `el_fmt_dec_i64`,
  `el_fmt_hex_u64`, `el_fmt_hex_bytes`; a printf subset in
  `elemental/fmt_printf.h` (`el_fmt_printf`, `el_fmt_vprintf`,
  `EL_FMT_PRINTF_CHECK`). Truncation keeps a prefix that ends on an item
  ([ADR 0011](docs/adr/0011-formatting-truncation.md)).
- `fmt`: the private choice `FMT_BUILTIN` / `FMT_LIBC`. `FMT_LIBC` converts
  numbers with the C library's `snprintf`, with identical output, and
  depends on the new probed symbol `PLATFORM_SNPRINTF`. No public header
  changes.
- `bytes` (experimental): `el_bytes_load_{le,be}{16,32,64}` and
  `el_bytes_store_{le,be}{16,32,64}`, at any alignment.
- `crc` (experimental): `el_crc32_update` (CRC-32/ISO-HDLC) and
  `el_crc16_ibm3740_update` (CRC-16/IBM-3740), with `EL_CRC32_INIT` and
  `EL_CRC16_IBM3740_INIT`.
- `bytes`: byte swap, `el_bytes_swap{16,32,64}`, and C23-style bit
  counting for 32 and 64 bits: `el_bytes_leading_zeros`,
  `el_bytes_trailing_zeros`, `el_bytes_count_ones`,
  `el_bytes_has_single_bit`, `el_bytes_bit_width`, `el_bytes_bit_floor`,
  `el_bytes_bit_ceil` (each suffixed `32` or `64`). A bit ceil that does not
  fit is 0.
- `bytes`: the rest of C23's counts, `el_bytes_leading_ones`,
  `el_bytes_trailing_ones`, `el_bytes_count_zeros`; `el_bytes_rotate_left`,
  `el_bytes_rotate_right` (any count, taken modulo the width), and
  `el_bytes_reverse_bits`; bit fields, `el_bytes_mask`,
  `el_bytes_field_get`, `el_bytes_field_set` (a field wider than its width is
  ignored; a field outside its type is a contract violation); and
  `el_bytes_align_up`, `el_bytes_align_down`, `el_bytes_is_aligned`, for a
  power-of-two alignment (anything else is a contract violation; `align_up`
  is 0 when the multiple does not fit). Each is suffixed `32` or `64`.
- `crc`: the private choice `CRC_TABLE` is now `CRC_IMPLEMENTATION`, with
  two more options, `CRC_GCC_BUILTIN` and `CRC_ZEPHYR`, offered where CMake
  probes them (`COMPILER_GCC_CRC_BUILTINS`, `PLATFORM_ZEPHYR_CRC`). Results
  are identical; the option symbols `CRC_TABLE_4BIT` and `CRC_TABLE_8BIT`
  are unchanged.
- `io`: a writer may return `EL_ERR_NO_SPACE`, not permanent, when it could
  not get storage. Only writers that allocate do.
- Tests are GoogleTest, pinned and built from source. The io contract suite
  is the GoogleTest suite `ElIoContract` (`elemental/io_contract.hpp`),
  installed as source; `el_io_contract_run()` and its report callback are
  gone. The fixture struct in `elemental/io_contract.h` is unchanged.
- `el_assert_failed()` no longer documents leaving by `longjmp()`. Nothing
  tests that any more, so it is no longer promised.
- The bare Cortex-M3 test image and its QEMU run were removed. Then the
  `cortex-m3` preset and its toolchain file were removed as well:
  microcontroller targets are Zephyr targets
  ([ADR 0010](docs/adr/0010-zephyr-targets.md)), and the tests and the
  symbol check run in the Zephyr test application (`tests/zephyr`).
- `io` (M3a): `el_buf` (`elemental/io_buf.h`), a memory buffer with fixed
  storage: `el_buf_init`, `el_buf_data`, `el_buf_data_mut`, `el_buf_len`,
  `el_buf_room`, `el_buf_append` (whole or nothing), `el_buf_reserve`,
  `el_buf_spare`, `el_buf_commit`, `el_buf_truncate`, and the memory
  endpoint `el_buf_as_writer`, which reports `EL_ERR_NO_SPACE` when full.
  `el_grow_buf` (`elemental/io_alloc.h`, `IO_ALLOC`): `el_grow_buf_init`,
  which returns its `el_buf *`, and `el_grow_buf_deinit`. They replace
  `el_mem_writer` (removed from `elemental/io_mem.h`) and `el_grow_writer`
  (removed). A full memory writer now reports `EL_ERR_NO_SPACE`, not
  `EL_ERR_END_OF_STREAM`; `EL_ERR_NO_SPACE` is no longer only from writers
  that allocate.
- `io_contract` (M3a): the optional fixture capability `writer_full_after`
  and the test `WriterNoSpaceKeepsWhatFitAndRepeats`. A fixture written with
  designated initializers before it existed leaves it null and skips it.
- `io` (M3a): `el_buffered_writer` (`elemental/io_buffered.h`), a buffer in
  front of another writer: `el_buffered_writer_init`, `_pending`, `_flush`,
  `_reserve`, `_as_writer`. Flush is a function of the type, not an
  operation of `el_writer` ([ADR 0014](docs/adr/0014-flush-on-the-concrete-type.md),
  provisional). `io` now needs `memmove` as well as `memcpy`.
- `fmt` (M3a): reworked on `io` ([ADR 0015](docs/adr/0015-formatting-into-io-buffers.md),
  superseding ADR 0011). `elemental/fmt.h` is three pure conversions,
  `el_fmt_dec_u64`, `el_fmt_dec_i64`, `el_fmt_hex_u64(out, cap, value[,
  flags])`, returning the length, with `EL_FMT_INT_LEN_MAX` (20) and
  `EL_FMT_UPPER` (now 1). `el_fmt_printf` and `el_fmt_vprintf` take an
  `el_buf *`; `el_fmt_printf_buffered` and `el_fmt_vprintf_buffered` are
  new, into an `el_buffered_writer`. Removed: `el_fmt`, `el_fmt_init`,
  `el_fmt_len`, `el_fmt_str`, `el_fmt_cstr`, `el_fmt_hex_bytes`,
  `EL_FMT_ZERO_PAD`, the width and flags of the conversions, NUL
  termination, and sticky truncation (truncation is per call). `fmt` now
  depends on `io` (Kconfig `select IO`). The private choice `FMT_BUILTIN` /
  `FMT_LIBC` is removed.
- `fmt` (M3a): the private choice `FMT_PRINTF_BUILTIN` (default) /
  `FMT_PRINTF_LIBC`, on what printf accepts. `FMT_PRINTF_LIBC` also accepts
  C's other numeric conversions, through the C library's `snprintf` one at
  a time, whole or absent, and `-` and `*` on `%s` and `%c`; the portable
  subset is built-in code under both. A private choice may now widen what a
  function accepts ([ADR 0016](docs/adr/0016-private-choices-that-widen.md)).
  No public header depends on it.
- Zephyr tests (M3a): one test image per module, selected by the CMake
  variable `ELEMENTAL_TEST_MODULE` of `tests/zephyr` (default `all`), and
  one Twister scenario per module, `elemental.tests.<module>`, on every
  board; the single `elemental.tests` scenario is gone. A test whose name
  is in no module's list stops the configure.
- The identifier prefix is `el_` / `EL_`, and generated configuration is
  `EL_CONFIG_`; before release it was `elem_` / `ELEM_` / `ELEM_CONFIG_`.
- `status` (M3b): `EL_NODISCARD`, now on every function that returns
  `el_status` and on `el_alloc`. **A call that drops a status is a compiler
  warning** under GCC and Clang, and in C23 and C++17 anywhere; with
  `-Werror`, an error. Handle the status, pass it on, or discard it on
  purpose with the new `el_status_ignore()`
  ([ADR 0018](docs/adr/0018-must-use-and-propagation.md)).
- `status` (M3b): `EL_TRY(expr)`, `EL_TRY_GOTO(expr, status, label)`,
  `EL_TRY_NEG(expr)`, and `EL_ENSURE(condition, status)`, in
  `elemental/status.h`, for passing a status on.
- `status` (M3b): negative `el_status` values are the application's.
  Elemental defines none and never returns one of its own; a function in
  front of an interface returns one unchanged when the application's
  implementation produced it
  ([ADR 0017](docs/adr/0017-negative-statuses.md)).
- `examples/consume_subdirectory`: a record too long for its length byte is
  the example's own `RECORD_ERR_TOO_LONG` (-1), and a text buffer too small
  is `EL_ERR_NO_SPACE`; both were `EL_ERR_IO`.
- `time` (M4, experimental): `el_ticks`, a 64-bit count of a clock's own
  ticks; the `el_clock` interface with `el_clock_now` and
  `el_clock_ticks_per_second`
  ([ADR 0020](docs/adr/0020-time-representation.md)); the conversions
  `el_ticks_from_ms` (rounds up) and `el_ticks_to_seconds` (seconds and
  microseconds, rounds down), which need no compiler helper;
  `el_manual_clock` (`elemental/time_manual.h`), a clock the caller
  advances; and the clock contract suite `ElTimeContract`
  (`elemental/time_contract.h`, `.hpp`).
- `time_posix` (M4, experimental): `el_posix_clock()`, `CLOCK_MONOTONIC` in
  nanoseconds.
- `time_zephyr` (M4, experimental): `el_zephyr_clock()`, the kernel's
  uptime in its ticks. The first backend only a Zephyr build can select:
  the probed Kconfig symbol `PLATFORM_ZEPHYR` is new.
- `log` (M4, experimental): `el_log_level` and four levels; `el_log_record`;
  the `el_log_sink` interface and `el_log_sink_write`; `el_logger`,
  `el_logger_init`, `el_logger_set_level`, and `el_log_write`, which is not
  variadic and returns nothing (`elemental/log.h`). `el_log_printf`,
  `el_log_vprintf`, and the macros `EL_LOG`, `EL_LOG_DEBUG`, `EL_LOG_INFO`,
  `EL_LOG_WARN`, `EL_LOG_ERROR` (`elemental/log_printf.h`).
  `el_log_writer_sink`, lines of text to an `el_writer`
  (`elemental/log_writer.h`), and `el_log_mem_sink`, records kept in caller
  memory (`elemental/log_mem.h`). The sink contract suite `ElLogContract`
  (`elemental/log_contract.h`, `.hpp`). The Kconfig option
  `LOG_MESSAGE_MAX` (128): the longest message `el_log_printf` formats, on
  its own stack.
- `log` (M4): `EL_LOG_CUTOFF`, a macro the **consumer** defines to remove
  `EL_LOG_*` calls below a level at compile time. Elemental's configuration
  never sets it. The first public macro whose expansion varies, and so the
  first exception to ADR 0004
  ([ADR 0021](docs/adr/0021-log-cutoff.md)).
- `examples/consume_installed`: `el_cat` logs what goes wrong through `log`
  to standard error, timed by `time_posix`, instead of calling `fprintf`.
  It now needs the package's `log` and `time_posix` components.
- `configs/variant_defconfig` also sets `LOG_MESSAGE_MAX=48`.
- `log`: `el_log_writer_sink_init(sink, out, line)` borrows the line buffer
  as an `el_buf *`, fixed (`el_buf_init`) or grown (`el_grow_buf_init`),
  in place of `(void *line_storage, size_t line_cap)`. The struct's `line`
  field is now that pointer, and the sink is 16 bytes on a 32-bit target
  instead of 28. A grown buffer lets a line be as long as its allocator
  allows; `EL_ERR_NO_SPACE` now means fixed storage too small or an
  allocator that refused. A null `line` is a contract violation; a sink
  that loses every record is one over an `el_buf` with no storage.
- `time`: `el_clock_ticks_from_ms(clock, ms)` and
  `el_clock_ticks_to_seconds(clock, ticks, &us)`, the two conversions in a
  clock's own unit, reading its frequency for the caller.
- `log` (experimental, [ADR 0022](docs/adr/0022-log-sink-lends-the-buffer.md)):
  a sink lends the buffer a record's message is written into.
  `el_log_sink_ops` has two operations in place of `write`: `begin(ctx,
  record, &message)` lends an `el_buf`, and `end(ctx, record, fit)` takes
  the record with its message in that buffer and whether it was cut.
  `el_log_printf` formats straight into the sink's buffer, with no buffer
  of its own; a message the buffer cannot take whole is cut, and both
  sinks count that record as lost (the writer sink still writes the cut
  line). The Kconfig option `LOG_MESSAGE_MAX` is removed, and with it the
  line in `configs/variant_defconfig`. `el_log_mem_sink` and
  `el_log_writer_sink` have new fields; the tee example's
  `tee_sink_init` takes a message buffer.
- `ring` (experimental, M5): `el_ring`, a byte FIFO over caller storage
  between one context that writes and one that reads, with no lock
  (`elemental/ring.h`): `el_ring_init`; the producer's `el_ring_write`,
  `el_ring_room`, `el_ring_close`, `el_ring_as_writer`; the consumer's
  `el_ring_read`, `el_ring_len`, `el_ring_as_reader`. A full ring's writer
  and an empty ring's reader report `EL_ERR_WOULD_BLOCK`
  ([ADR 0009](docs/adr/0009-capacity-status.md), amended). The Kconfig
  option `RING`, which depends on the new probed symbol
  `COMPILER_ATOMICS`. The only atomic operations are a load and a store of
  one word ([ADR 0023](docs/adr/0023-atomics-and-interrupt-context.md)).
- `log` (experimental, M5): `el_log_deferred_sink`
  (`elemental/log_deferred.h`), a sink that copies each record into an
  `el_ring` for another context to write:
  `el_log_deferred_sink_init`, `el_log_deferred_sink_as_sink`,
  `el_log_deferred_sink_drain`, `el_log_deferred_sink_lost`,
  `el_log_deferred_sink_record_size`. Part of `elemental::log` when the
  new Kconfig option `LOG_DEFERRED` is on (the default where `RING` is),
  which makes `elemental::log` link `elemental::ring`.
- The ring pipe example, `examples/backend_ring`, is removed: `ring`
  replaces it. `examples/consume_installed` runs the installed io contract
  suite against the installed `ring` and needs the package's `ring`
  component. `examples/uart_ring` is new: a Zephyr UART's receive
  interrupt behind an `el_reader`, logging through the deferred sink.
- A `tsan` preset: Clang with ThreadSanitizer, running the tests of `ring`
  and of the deferred sink, two of which use two threads.
- `io`: documented that the `el_buf` growth hook is private to io and is
  installed only by `el_grow_buf_init`.
- `fmt` and `log` consumers can opt into `elemental_check_formats(target)`
  to reject nonportable printf literals at build time. It works from an
  installed package or a CMake subproject, with an option for targets that
  require `FMT_PRINTF_LIBC`.
- `io_contract` (M5a, [seams proposal](docs/proposals/seams.md)): a
  fixture has a reader half and a writer half, and may leave either out;
  the suite reports the absent half's tests as skipped ("no reader", "no
  writer"). The new field `reader_then`, `EL_IO_CONTRACT_THEN_ENDS` (0, the
  default) or `EL_IO_CONTRACT_THEN_STALLS`, says what a reader does after
  its content; it is the fixture's last member, after the ones that were
  there, which keep their order, so a fixture initialized by position still
  compiles and means what it did. The content the suite lends a reader is
  one constant of static storage duration, valid for as long as the reader
  borrows it. `ReaderDeliversItsContentInOrderThenEnds` is renamed
  `ReaderDeliversItsContentInOrder`, and the new
  `ReaderThatStallsAfterItsContentHasNotEnded` checks a reader that stalls:
  13 tests, from 12. `reset` is optional. The buffered-writer and
  `el_grow_buf` fixtures no longer borrow a memory reader; the ring is
  also run open, as a reader that stalls.
- `time_contract` (M5a): `reset` is optional.
- `log` (experimental, M5a): `el_log_compose(logger, level, compose, ctx)`
  and `el_log_compose_fn`: an application's function writes the message
  into the buffer the sink lends, between begin and end, which the library
  calls, ending the record on the sink that began it even if the function
  changed the logger. Nothing is composed for a record that is not made or
  that the sink refuses at begin; a sink that refuses it at end does so
  after the function ran. The function runs in the caller's context, so
  from an interrupt handler it must not block or allocate. `EL_LOG_KEPT`
  (`elemental/log_printf.h`), the cut-off as a constant, which `EL_LOG`
  now uses. `el_logger_off()`, a constant logger that makes no records.
  `EL_LOG_MEM_SINK_STORAGE_SIZE(count, message_max)` and
  `EL_LOG_DEFERRED_SINK_STORAGE_SIZE(waiting, message_max)` size a sink's
  storage at compile time, checking no overflow; the `_record_size()`
  functions stay. The memory, writer and deferred sinks gain a field
  marking a record open, and assert that logging into a sink from inside a
  compose function, or into a writer sink from its own writer, does not
  begin a second record; the writer sink's record stays open until its
  line is written. `reset` is optional in the log contract suite.
- `log` (experimental, M5a), breaking: the deferred sink owns its queue.
  `el_log_deferred_sink_init(sink, storage, storage_cap, message_max)`
  takes one block of storage and the longest message, in place of an
  `el_ring *` and record storage; a size above 0 and below one record, or
  a queue above `SIZE_MAX / 2`, is a contract violation, and a size of 0
  makes a sink that loses everything whatever `message_max` is.
  `el_log_deferred_sink_pending_bytes()` reports the queue's fill.
- `alloc` (experimental, M5a, [ADR 0025](docs/adr/0025-an-allocation-changes-size-and-may-move.md)):
  `el_allocator_ops` gains an optional last operation, `realloc`, which
  changes an allocation's size and may move it; null from it means "no
  better than allocate, copy and free". `el_realloc()` asks it and falls
  back to that; after a success only the returned pointer is used, even
  at the old address, and the allocation is freed with its new size.
  Both arenas grow and shrink their latest allocation in place;
  `el_libc_allocator()` answers with `realloc()` up to `max_align_t`, and
  asks `malloc()` and `realloc()` for at least the alignment in bytes, so
  that a heap that aligns small blocks less still returns them aligned;
  the checking allocator checks a resize that reaches it as it checks a
  free. `alloc` now needs `memcpy`. `reset` is optional in the alloc
  contract suite, which gains 13 tests of resizing.
- `io` (M5a): `el_grow_buf` grows through `el_realloc`, so a buffer that is
  its arena's latest allocation grows in place.
- `io_zephyr_uart` and `ring_zephyr_uart` (experimental, M5a): a Zephyr
  UART, as two libraries named for the facility
  ([conventions.md](docs/conventions.md#naming)).
  `io_zephyr_uart` (`elemental/io_zephyr_uart.h`,
  `elemental::io_zephyr_uart`) is `el_zephyr_uart_poll`, a writer that
  passes each byte to `uart_poll_out()`. Its promise is that every byte
  reaches `uart_poll_out()`: lossless only where the driver's `poll_out`
  waits for the device to accept it (Zephyr's RTT UART skips bytes when its
  buffer is full). Kconfig `IO_ZEPHYR_UART`, which depends on `IO` and on
  the new probed symbol `PLATFORM_ZEPHYR_UART` (`CONFIG_SERIAL`); it needs
  neither interrupt-driven UARTs nor `ring`.
  `ring_zephyr_uart` (`elemental/ring_zephyr_uart.h`,
  `elemental::ring_zephyr_uart`) is `el_zephyr_uart_irq`, the producer of a
  ring the caller sets up: the receive interrupt writes what arrives into
  it, counts what did not fit, and calls the application's
  `on_arrival(ctx)` after bytes arrive, queued or counted as dropped. The
  thread reads through `el_ring_as_reader()`. From a successful init until
  deinit the ring's producer side is the handler's; a failed init leaves
  the ring open and untouched; deinit disables the interrupt, clears the
  callback, then closes the ring, so the reader ends after what was
  received. `on_arrival` may also run inside `el_zephyr_uart_irq_init`, on
  the calling thread, so it and its `ctx` are ready before init; the ring
  and its storage stay alive while its reader is used, after deinit too.
  The callback is set only after the caller's ring is open, as some drivers
  run it at once from another of the device's interrupts; init fails with
  `EL_ERR_IO` when an asynchronous receive is active (`uart_poll_in()`
  gives `-EBUSY`); a driver without `poll_in` cannot have held bytes
  discarded, and they may be received. The receiver is supported only on
  drivers that call the callback from the device's interrupt, not from a
  work queue (Bluetooth NUS, USB CDC ACM), and only on an instance with
  interrupt-driven receive, which Zephyr cannot report per instance.
  `EL_ERR_IO` covers only what the driver reports. Kconfig
  `RING_ZEPHYR_UART`, which depends on `RING` and on the new probed symbol
  `PLATFORM_ZEPHYR_UART_IRQ` (`CONFIG_SERIAL` and
  `CONFIG_UART_INTERRUPT_DRIVEN`), a build-wide fact. Neither public header
  includes a Zephyr header, and no public layout depends on Zephyr's
  configuration. The io contract suite runs over two joined UARTs on
  `mps2/an385`, the writer on one and the ring's reader on the other, in
  the `elemental.tests.ring.uart` scenario.
- `io_zephyr_uart`, `ring_zephyr_uart`, `time_zephyr`, `crc` with
  `CRC_ZEPHYR` (M5a): Zephyr's `zephyr_interface` is linked for the build
  only, so a Zephyr application can add Elemental with
  `ELEMENTAL_INSTALL=ON` and install it; the export failed to generate
  before.
- `examples/uart_ring` (M5a): its receive side is now `ring_zephyr_uart`
  filling the example's own ring, and it sends with `io_zephyr_uart`;
  `uart_reader.c` and `.h` are removed. What it still shows is logging from
  an interrupt handler, a `k_timer`'s, through the deferred sink.
- Zephyr tests: `native_sim`'s image turns its pseudo-terminal UARTs on and
  interrupt-driven (`tests/zephyr/boards/native_sim.conf`), the first one
  staying the console on standard output, so `io_zephyr_uart` and
  `ring_zephyr_uart` build there and their tests against scripted devices
  run in `elemental.tests.io` and `elemental.tests.ring`.
- With `ELEMENTAL_INSTALL=ON` in a build whose toolchain skips CMake's
  compiler detection (a Zephyr application), Elemental measures the
  target's pointer size itself before including `GNUInstallDirs`, which no
  longer warns that it cannot choose `lib` or `lib64`.
