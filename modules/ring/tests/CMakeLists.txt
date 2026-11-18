# Interface semantics: the io contract suite, against the ring's writer and
# reader.
elemental_add_test(ring.contract
  SOURCES contract_ring_test.cpp fixture_ring.c
  LIBRARIES elemental::ring elemental::io_contract)

# What only the ring promises, one context driving both sides: every byte
# of the storage used, order across its end, room and len, closing.
elemental_add_test(ring.unit
  SOURCES ring_test.cpp
  LIBRARIES elemental::ring)

# Two contexts at once: a thread on each side. Host only: a target's test
# image has one thread (its interrupt test is examples/uart_ring). The run
# that can fail on a data race is the `tsan` preset's (docs/build.md).
if(NOT ELEMENTAL_TEST_IMAGE)
  find_package(Threads REQUIRED)
  elemental_add_test(ring.stress
    SOURCES ring_stress_test.cpp
    LIBRARIES elemental::ring Threads::Threads)
endif()

# Contract checks fire, in whichever way the configuration makes them fire.
# Death tests: host only, and only with assertions compiled in.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(ring.violations
    SOURCES ring_violations_test.cpp
    LIBRARIES elemental::ring)
endif()
