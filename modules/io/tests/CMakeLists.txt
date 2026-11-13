# Interface semantics: the shared contract suite, against the memory reader
# and a fixed el_buf as a writer.
elemental_add_test(io.contract.buf
  SOURCES contract_buf_test.cpp fixture_buf.c
  LIBRARIES elemental::io_contract)

# What only el_buf promises: whole-or-nothing append, writing in place,
# truncation, and the memory endpoint.
elemental_add_test(io.unit.buf
  SOURCES io_buf_test.cpp
  LIBRARIES elemental::io)

# The helpers, driven by a scripted stream so every interleaving of progress,
# would-block, and failure is reproducible.
elemental_add_test(io.helpers
  SOURCES io_helpers_test.cpp
  LIBRARIES elemental::io)

# Contract checks fire, in whichever way the configuration makes them fire.
# With assertions compiled out there is nothing to observe, and without a
# process to fork (on a target) there is no death test.
if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
  elemental_add_test(io.violations
    SOURCES io_violations_test.cpp
    LIBRARIES elemental::io)
endif()

if(EL_CONFIG_IO_ALLOC)
  # Interface semantics: the io contract suite, against an el_grow_buf.
  elemental_add_test(io.contract.grow_buf
    SOURCES contract_grow_buf_test.cpp fixture_grow_buf.c
    LIBRARIES elemental::io_contract elemental::alloc_contract)

  # What only el_grow_buf promises: growth, the capacity status, and that it
  # frees exactly what it allocated.
  elemental_add_test(io.unit.grow_buf
    SOURCES io_alloc_test.cpp
    LIBRARIES elemental::io elemental::alloc_contract)

  if(EL_CONFIG_ASSERT AND ELEMENTAL_DEATH_TESTS)
    elemental_add_test(io.violations.grow
      SOURCES io_alloc_violations_test.cpp
      LIBRARIES elemental::io)
  endif()
endif()
