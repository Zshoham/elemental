# A clock written the way one outside this repository would be: it links
# elemental::time, implements the interface, and proves itself with the
# shared contract suite.
add_library(example_step_clock STATIC step_clock.c)
target_include_directories(example_step_clock PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(example_step_clock PUBLIC elemental::time)
elemental_target_c_standard(example_step_clock)
elemental_target_warnings(example_step_clock)

if(COMMAND elemental_add_test)
  # What only this clock promises.
  elemental_add_test(example.step_clock.backend
    SOURCES step_clock_test.cpp
    LIBRARIES example_step_clock)

  # What every clock promises.
  elemental_add_test(example.step_clock.contract
    SOURCES contract_step_clock_test.cpp step_clock_fixture.c
    LIBRARIES example_step_clock elemental::time_contract)
endif()
