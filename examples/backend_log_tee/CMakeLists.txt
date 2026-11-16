# A log sink written the way one outside this repository would be: it links
# elemental::log, implements the interface, and proves itself with the
# shared contract suite.
add_library(example_tee_sink STATIC tee_sink.c)
target_include_directories(example_tee_sink PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(example_tee_sink PUBLIC elemental::log)
elemental_target_c_standard(example_tee_sink)
elemental_target_warnings(example_tee_sink)

if(COMMAND elemental_add_test)
  # What only this sink promises: both get the record, and whose status
  # comes back.
  elemental_add_test(example.tee_sink.backend
    SOURCES tee_sink_test.cpp refusing_sink.c
    LIBRARIES example_tee_sink)

  # What every sink promises.
  elemental_add_test(example.tee_sink.contract
    SOURCES contract_tee_sink_test.cpp tee_sink_fixture.c
    LIBRARIES example_tee_sink elemental::log_contract)
endif()
