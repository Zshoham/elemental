# An io backend written the way one outside this repository would be: it
# links elemental::io, implements the interface, and proves itself with the
# shared contract suite.
add_library(example_ring_pipe STATIC ring_pipe.c)
target_include_directories(example_ring_pipe PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(example_ring_pipe PUBLIC elemental::io)
elemental_target_c_standard(example_ring_pipe)
elemental_target_warnings(example_ring_pipe)

if(COMMAND elemental_add_test)
  # What only this backend promises. The contract suite cannot know that a
  # ring pipe has a write side to close.
  elemental_add_test(example.ring_pipe.backend
    SOURCES ring_pipe_test.cpp
    LIBRARIES example_ring_pipe)

  # What every reader and writer promises.
  elemental_add_test(example.ring_pipe.contract
    SOURCES contract_ring_test.cpp ring_pipe_fixture.c
    LIBRARIES example_ring_pipe elemental::io_contract)
endif()
