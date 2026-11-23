# time

A clock interface, time as a count of the clock's own ticks, conversion to
and from human units, a clock that tests advance by hand, backends for
POSIX and Zephyr, and a contract suite for clocks.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::time`, `elemental::time_contract`, `elemental::time_posix`, `elemental::time_zephyr` |
| Depends on | `status` (assertions only). Runtime: nothing, and no compiler helper. |
| Allocates | No |
| Kconfig | `TIME`, `TIME_POSIX`, `TIME_ZEPHYR` |

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_ticks`, `el_clock`, `el_clock_now`, `el_clock_ticks_per_second` | Interface contract | [`elemental/time.h`](include/elemental/time.h) |
| `el_ticks_from_ms`, `el_ticks_to_seconds` | Pure functions | the same |
| `el_clock_ticks_from_ms`, `el_clock_ticks_to_seconds` | The same conversions, in a clock's own unit | the same |
| `el_manual_clock` | Portable implementation | [`elemental/time_manual.h`](include/elemental/time_manual.h) |
| The clock contract suite | GoogleTest tests, shipped as source | [`elemental/time_contract.hpp`](contract/include/elemental/time_contract.hpp), fixture in [`elemental/time_contract.h`](contract/include/elemental/time_contract.h) |
| `el_posix_clock` | Platform backend | [`elemental/time_posix.h`](backends/posix/include/elemental/time_posix.h), [PORT.md](backends/posix/PORT.md) |
| `el_zephyr_clock` | Platform backend | [`elemental/time_zephyr.h`](backends/zephyr/include/elemental/time_zephyr.h), [PORT.md](backends/zephyr/PORT.md) |

Start with `time.h`. The design and the examples that shaped it are in the
[proposal](../../docs/proposals/time.md); why a time value is ticks of the
clock's own frequency and not a fixed unit is
[ADR 0020](../../docs/adr/0020-time-representation.md).

## Using it

Take an `el_clock` as a parameter wherever you need to know what time it
is. Nothing in Elemental reads a clock it was not handed, so the caller
decides which one: the system's in a program, a manual one in a test.

```c
const el_ticks start = el_clock_now(clock);
/* ... */
const el_ticks took = el_clock_now(clock) - start;

uint32_t us;
const uint64_t seconds = el_clock_ticks_to_seconds(clock, took, &us);
```

A reading is a 64-bit count of the clock's ticks. Readings of one clock
compare and subtract as plain integers: the clock never goes back and the
count does not wrap. A deadline is a reading:

```c
const el_ticks deadline = el_clock_now(clock) + el_clock_ticks_from_ms(clock, 250);
/* ... */
if (el_clock_now(clock) >= deadline) {
    /* timed out */
}
```

Three things to keep in mind:

- **A reading means something only with its clock.** Readings of two
  clocks do not compare. Whatever hands a reading to code that must print
  it hands the frequency too, as a log record does.
- **Subtract the earlier from the later.** The type is unsigned, so
  `deadline - now` wraps once the deadline has passed. Compare first.
- **Converting is a function call.** `el_ticks_from_ms` rounds up, so a
  timeout never ends early; `el_ticks_to_seconds` rounds down, so a
  printed time has passed. They are for setting a timeout up and for
  printing, not for a loop that runs per tick. The `el_clock_` forms
  read the clock's frequency for you; a value that has left its clock,
  such as a log record's timestamp, is converted with the frequency it
  carries.

There is no wall time, no calendar, and no sleep here. Nothing in this
module waits.

### Testing code that depends on time

Give it a manual clock and move the clock yourself. "250 ms later" is then
exact, and the test takes no time:

```c
el_manual_clock manual;
el_manual_clock_init(&manual, 1000); /* a tick is a millisecond */

el_manual_clock_advance(&manual, 250);
assert(el_clock_now(el_manual_clock_as_clock(&manual)) == 250);
```

The [proposal's second example](../../docs/proposals/time.md#example-2-a-timeout-and-a-test-of-it-that-takes-no-time)
tests a read with a timeout this way, against a peer that stalls.

### Choosing a clock

| Clock | A tick is | Reads | For |
|---|---|---|---|
| `el_manual_clock` | what you say | what it was advanced to | Tests; a program that has no clock yet |
| `el_posix_clock()` | a nanosecond | `CLOCK_MONOTONIC` | A hosted program |
| `el_zephyr_clock()` | a kernel tick, `CONFIG_SYS_CLOCK_TICKS_PER_SEC` to the second | `k_uptime_ticks()` | A Zephyr application |

### Contract violations

Reading a clock cannot fail, so nothing here returns a status. What only a
bug can produce is asserted
([conventions.md](../../docs/conventions.md#arguments)):

| Contract violation, asserted | |
|---|---|
| A handle that was never made, or one with an operation missing | A calling bug |
| A clock that states a frequency of 0 | A broken backend, caught by `el_clock_ticks_per_second` |
| A frequency of 0 passed to a conversion; a null `us` | A calling bug |
| A null manual clock; one given a frequency of 0; one advanced past the largest reading | A calling bug |

A clock that goes back is a broken backend too, but the front function
has no state to see it with: the contract suite is what catches it.

## No compiler helper

On 32-bit Arm, `/`, `%` and `*` on a 64-bit operand are calls into the
compiler's runtime (`__aeabi_uldivmod`, `__aeabi_lmul`), which Elemental's
libraries do not rely on
([allowed-undefined.txt](../../tests/zephyr/allowed-undefined.txt)). The
conversions divide by shift and subtract and multiply from 16-bit halves
instead, as `fmt` and `alloc` do. Measured for the Cortex-M0+, the pair is
282 bytes that way, against 84 bytes plus 504 of libgcc
([proposal](../../docs/proposals/time.md#dependencies-and-cost)). Adding,
subtracting and comparing readings is inline code everywhere.

## Implementing a clock

1. Define the two operations of `el_clock_ops` over your own state: the
   reading, which never goes back and never blocks, and the frequency,
   which never changes and is at least 1.
2. Put them in a `static const` table and write a function that returns a
   handle.
3. Write a fixture in C and instantiate the contract suite with it.
4. Write tests for what only your clock promises: which clock it is, and
   its unit.

[examples/backend_step_clock](../../examples/backend_step_clock) is a
complete clock done this way: one that moves a fixed step each time it is
read.

## The contract suite

`ElTimeContract` checks what the interface promises, against any clock a
fixture describes:

```cpp
#include <elemental/time_contract.hpp>

#include "my_fixture.h"

INSTANTIATE_TEST_SUITE_P(MyClock, ElTimeContract, testing::Values(&my_fixture));
```

A fixture has one required function, the clock, and two optional ones.
**reset** releases what the previous request made; a fixture with nothing
to release leaves it out, and the suite does not call it. **advance**
returns once at least so many of the clock's ticks have passed, by waiting
for a clock that runs on its own or by moving one that does not. With it
the suite checks that a reading grows by at least the time that passed,
and that readings do not go back while it does.

| Implementation | Tests run | Skipped, and why |
|---|---|---|
| Manual clock | 5 | 0 |
| POSIX clock | 5 | 0 |
| Zephyr clock | 5 | 0 |
| Step clock example | 3 | 2: it moves only when read, so nothing outside it can make time pass |

Deliberately broken clocks were used to check the suite itself; the
results are in [docs/build.md](../../docs/build.md#what-is-verified).

## Tests

| Test | What it checks |
|---|---|
| `time.contract.manual.*` | The contract suite against the manual clock. |
| `time.unit.*` | The front functions pass the implementation's answers and context through; both conversions at their edges (no time, one tick either side of a second, the largest count, frequencies from 1 to 2³²−1), their rounding, and agreement with the compiler's division for random values at thirteen frequencies; that the clock-taking forms use the clock's own frequency and read no time; the manual clock. |
| `time.violations.*` | Each documented contract violation stops the program. Death tests, host only. |
| `time_posix.contract.*`, `time_posix.platform.*` | See [PORT.md](backends/posix/PORT.md). |
| `time_zephyr.contract.*`, `time_zephyr.platform.*` | See [PORT.md](backends/zephyr/PORT.md). Zephyr only. |

## Known gaps

Deferred until a consumer needs them: a signed or saturating "ticks until"
for the time left before a deadline; milliseconds out; a clock faster than
2³²−1 ticks per second, which today scales its reading down. No clock has
been run on real hardware.
