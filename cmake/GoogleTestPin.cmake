# The one place that names the GoogleTest revision Elemental's tests are
# built with.
#
# GoogleTest is a test-only dependency: Elemental's libraries do not use it,
# and a consumer who builds no Elemental tests never needs it. It is built
# from source as part of the test build, so it always matches the compiler
# and flags the tests are built with (sanitizers included).
#
# Bump the revision here, re-run `cmake -P tools/provision.cmake`, and run the
# full test suite; nothing else records the revision.

set(ELEMENTAL_GOOGLETEST_REPOSITORY "https://github.com/google/googletest.git")
# Tag v1.18.0.
set(ELEMENTAL_GOOGLETEST_REVISION "063de7e9578f82b369302001269680b4b1553359")
