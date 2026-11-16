# The clock contract test suite. Any el_clock implementation, in this
# repository or outside it, links this and instantiates the suite with its
# own fixture.
elemental_add_contract_suite(time_contract
  SOURCES src/time_contract.cpp
  DEPENDS time)
