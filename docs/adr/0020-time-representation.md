# 0020. Time is a 64-bit count of a clock's own ticks

Status: Accepted (2026-11-16). Decided by the project owner with the
[time proposal](../proposals/time.md) (decision 1), against a fixed unit of
nanoseconds. It is the answer
[module-plan.md](../module-plan.md#3-decisions-that-must-come-first)
recommended; the roadmap asks for it before the log record is designed.

## Context

Three things need a time value: a log record's timestamp, a timeout, and a
deadline. The choice is between a unit fixed by Elemental (nanoseconds,
say) and the unit of whatever clock the caller has.

*Source facts* (Zephyr v4.5.0, read locally): the kernel counts in ticks
of a build-time frequency, 100 per second by default and 10 000 for a
tickless kernel; `k_uptime_ticks()` returns them as 64 bits;
`k_uptime_get()`, in milliseconds, is a conversion of that on every read;
and its logging takes a raw timestamp function together with a frequency
(`log_set_timestamp_func(getter, freq)`) and divides only when it prints.

*Measured* (Cortex-M0+, Zephyr SDK GCC 14.3.0, `-Os`): a 64-bit division
or multiplication is a call into the compiler's runtime, 504 bytes of
libgcc for the pair. Reading a clock in a fixed unit would put one on the
path of every log record and every deadline check. Adding and comparing
64-bit values is inline.

Elemental cannot make the frequency a public compile-time constant, as
Zephyr does: configuration is private
([ADR 0004](0004-private-configuration.md)).

## Decision

- **A time value is `el_ticks`, a `uint64_t` count of one clock's ticks**:
  an instant, counted from an origin the clock chooses, or a duration.
- **The clock states its frequency**, ticks per second, as a `uint32_t`
  that never changes. A value means something only with the clock it came
  from.
- **Add, subtract, and compare are plain integer operations.** A clock
  never goes back, so there is no wrap rule: at a tick per nanosecond the
  count lasts 584 years.
- **Converting to or from a human unit is an explicit function call**
  (`el_ticks_from_ms`, `el_ticks_to_seconds`), which is where the division
  is. It rounds a timeout up and a printed time down.
- **Whatever carries a time value on to someone who must print it carries
  the frequency too.** A log record has both.
- No wall time, no calendar, no sleep.

## Cost

A reading is not self-describing: code that prints or sets a timeout
needs the clock or its frequency beside the value, and readings of two
clocks must not be mixed, which the type does not prevent. Every
conversion is a function call that divides. The unsigned type makes
`deadline - now` wrap once the deadline has passed, so callers compare
first. A clock faster than 2³²−1 ticks per second has to scale its reading
down.

## Revisit when

- Passing the frequency around is found to cause bugs, or to be most of
  what callers write. A fixed unit is the alternative, and the measurement
  to repeat is the division on the read path.
- A consumer needs to compare readings from two clocks.
- A consumer needs remaining time often enough to want a signed duration.
