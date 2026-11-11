# An allocator written the way one outside this repository would be: it
# links elemental::alloc, implements the interface, and proves itself with
# the shared contract suite.
add_library(example_size_classes STATIC size_classes.c)
target_include_directories(example_size_classes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(example_size_classes PUBLIC elemental::alloc)
elemental_target_c_standard(example_size_classes)
elemental_target_warnings(example_size_classes)

if(COMMAND elemental_add_test)
  # What only this allocator promises.
  elemental_add_test(example.size_classes.backend
    SOURCES size_classes_test.cpp
    LIBRARIES example_size_classes)

  # What every allocator promises.
  elemental_add_test(example.size_classes.contract
    SOURCES contract_size_classes_test.cpp size_classes_fixture.c
    LIBRARIES example_size_classes elemental::alloc_contract)
endif()
