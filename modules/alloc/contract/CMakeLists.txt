# The allocator contract test suite, and the checking allocator. Any
# el_allocator implementation, in this repository or outside it, links this
# and instantiates the suite with its own fixture; any test that allocates
# can wrap its allocator in the checker.
elemental_add_contract_suite(alloc_contract
  SOURCES src/alloc_contract.cpp src/alloc_checker.c
  DEPENDS alloc)
