---
name: writing-tests
description: Write or change Elemental tests - GoogleTest unit tests, death tests for contract violations, contract-suite tests, fixtures, and their CMake registration. Use when adding, editing, or debugging any test under modules/, examples/, or tests/.
---

# Writing tests

Tests are GoogleTest in C++17; the libraries they test are C11
(`docs/adr/0008-googletest.md`). gMock is not built.

A test is done when it has been seen **red**: it fails against an
implementation that breaks the promise it names. Then run `preflight`.

## Which test

| The promise belongs to | Write | Lives in |
|---|---|---|
| A function's documented behavior | A `TEST` or `TEST_F` | `modules/<module>/tests/` |
| An interface, for every implementation | A `TEST_P` in its contract suite | `modules/<module>/contract/src/` |
| One backend only | A test beside that backend | `modules/<module>/backends/<platform>/tests/` |
| "This is a contract violation" in a header | A death test | `modules/<module>/tests/*_violations_test.cpp` |

A behavior in a header and in none of these is unverified. Before writing,
read the nearest existing test of the same kind and follow its shape.

## Registering

```cmake
if(COMMAND elemental_add_test)
  elemental_add_test(<module>.<kind>
    SOURCES tests/<file>_test.cpp [tests/<fixture>.c]
    LIBRARIES elemental::<module>)
endif()
```

`<kind>` in use: `unit`, `helpers`, `violations`, `contract[.<implementation>]`,
`platform`. Each `TEST` becomes the CTest test `<module>.<kind>.<Suite>.<Test>`,
so `ctest --preset <p> -R <module>.<kind>` runs one group. The binary is
`build/<preset>/<directory>/test_<module>_<kind>` and takes
`--gtest_filter`. The function (`cmake/ElementalTesting.cmake`) links
`main()` and, in the application-handler configuration, an
`el_assert_failed()` that aborts.

The same registration compiles the test into the Zephyr test image
(`tests/zephyr`, `cmake/ElementalTestImage.cmake`), where every test runs in
one program. So test names must be unique across the repository, global C
symbols in fixtures must not clash, and a test that needs the host (a
file descriptor, a fork) is registered only where the host provides it.
Keep large buffers off the stack or within the image's 4 KiB main stack
(`tests/zephyr/prj.conf`). On Zephyr each module's tests are their own
image, chosen by test-name prefix (`ELEMENTAL_TEST_MODULE_<module>` in
`tests/zephyr/CMakeLists.txt`); a test whose prefix is in no module's list
stops the Zephyr configure. An example's tests go with the module they
test.

## Rules

- **Name a test as the promise it checks**, a sentence in CamelCase. A
  failing name should read as the broken promise.
- **C stays C.** Whatever describes a C implementation (a fixture, a
  backend, a scripted implementation's operations) is a `.c` file linked
  into the test, with `extern "C"` guards in its header. Only the tests are
  C++.
- **Cover the failure paths and the boundaries**: zero sizes, null with
  zero, every status a function documents, and what each output parameter
  holds for each.
- **Statuses print as integers** in a failure. Where the number alone would
  not explain it, append `<< el_status_name(status)`.
- Name helper classes anything but `Run`: `testing::Test::Run()` is private
  and the clash is a confusing compile error.

## Death tests

Follow the frame of an existing `*_violations_test.cpp`:

- The suite alias ends in `DeathTest`, which GoogleTest runs first.
- Accept any death (`EXPECT_DEATH(statement, "")`), so the test holds
  whether a failed check traps or calls the handler.
- `SetUpTestSuite()` sets `RLIMIT_CORE` to 0 **and** `PR_SET_DUMPABLE` to 0.
  With only the limit, every run leaves one crash record per violation in
  the system journal.
- A plain test next to them shows that correct use, edge cases included,
  trips no check.
- Register it inside `if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)`: with
  assertions compiled out there is nothing to observe, and a Zephyr target
  cannot fork, so death tests run on the host only.

## Contract-suite tests

A contract test states an interface promise against a fixture, so it knows
nothing about any one implementation.

- When the fixture lacks the capability the test needs, `GTEST_SKIP()` with
  the reason. A fixture leaves a capability null; it never fakes one.
- Adding or removing a contract test changes recorded counts; `preflight`
  step 5 lists where.

## Seeing it red

Break the code under test and check that the new test notices:

```sh
.agents/skills/writing-tests/scripts/mutant.sh        # first: the harness is sound, exit 0
.agents/skills/writing-tests/scripts/mutant.sh <file> '<old text>' '<new text>' ['<ctest regex>']
```

It applies the replacement to a copy of the tree under `build/mutant/`,
builds it, and reports which tests failed. The real sources are untouched.
For a contract test, break an implementation the suite runs against (an
example backend is the cheapest). `SURVIVED` means the promise has no test:
add one, or assign it to the backend's own tests if the interface does not
promise it.
