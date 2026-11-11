# 0008. Tests are GoogleTest; target tests go through Zephyr

Status: Accepted (2026-11-11). Decided by the project owner. The `cortex-m3`
preset it keeps is superseded by [ADR 0010](0010-zephyr-targets.md); the
Zephyr results are recorded in [build.md](../build.md#tests-on-zephyr).

## Context

The first slice tested itself with what it built: a 37-line check macro for
unit tests, and a contract suite written in freestanding C that reported
through a callback. The point of the second was that the same suite ran on
a host and, linked into a bare Cortex-M3 image, under QEMU.

That bought on-target evidence at the price of comfort. Tests had no names
to filter by, failures printed a condition and not the values, violations
could only be observed in one configuration through `longjmp`, and every
future suite would have needed the same hand-made machinery.

GoogleTest was prototyped as a host front end over the C suite and worked.
It cannot replace the C suite on a bare target: it needs a hosted C++17 library, and its headers do not
compile for `thumbv7m-none-eabi` without one.

## Decision

**All of Elemental's tests are GoogleTest**, including contract suites.

- GoogleTest is pinned to a commit and provisioned like Kconfiglib. It is
  built from that checkout with the tests' own compiler and flags.
- A contract suite is a value-parameterized GoogleTest suite. Its parameter
  is a fixture, which stays a C struct of functions, so a backend written in
  C is described in C. A backend author adds one line of C++.
- A contract suite is distributed as source and compiled into the test that
  links it. An archive would have to match the consumer's GoogleTest build
  and compiler, and test registrations in an archive are dropped by the
  linker unless something refers to them.
- Contract violations are tested as death tests, in every configuration
  that has assertions on.
- The libraries stay C11. C++ and GoogleTest are needed to build tests and
  for nothing else; an installed package contains neither.

**The bare Cortex-M3 test image is removed.** The `cortex-m3` preset keeps
the compile check and the symbol check, which need no test framework.

**Tests on microcontroller targets will run under Zephyr**, which provides
the hosted C++ library, startup code, and runtime that GoogleTest needs and
that Elemental does not want to own
([ADR 0001](0001-scope.md)). Twister, Zephyr's test runner, has a `gtest`
harness for this.

## Cost

- **No test currently runs on a non-host target.** The emulator evidence the
  first slice had is gone until the Zephyr work lands. Until then every
  behavioral claim is a host claim.
- Running the tests needs a C++17 compiler and a second pinned dependency.
- Tests are no longer C, so they stop doubling as C usage examples. The
  examples and fixtures remain C.
- A backend author outside this repository needs GoogleTest to run the
  contract suite, and a target whose toolchain has no hosted C++ library
  cannot run it at all.
- Test binaries and compile times are larger. This was accepted: build-time
  cost does not reach a product.
- This supersedes the parts of [ADR 0007](0007-first-slice.md) and of the
  first slice's design that describe a freestanding contract suite.

## Verified and not

Verified: the suite passes for four fixtures on a Linux host with GCC and
Clang and under sanitizers; the installed-package consumer builds it
against its own GoogleTest; it catches the same deliberately broken
backends the C suite caught.

Not verified: anything about Zephyr. That Twister has a `gtest` harness was
read in Zephyr's source (`scripts/pylib/twister/twisterlib/harness.py`,
`main` at `b7a1d910fcb8`). Zephyr's manifest at that revision lists no
GoogleTest module, so the application supplies it. Whether GoogleTest
1.18 builds for a Cortex-M Zephyr target, and how much memory it needs
there, has not been tried.

## Revisit when

- GoogleTest cannot be made to run on the Zephyr targets that matter, or
  needs more memory than they have. Then on-target testing needs a smaller
  framework with the same macros (Pigweed's `pw_unit_test` is one), or the
  contract suites need a second, smaller form.
- A backend must be verified on a target that Zephyr does not support.
