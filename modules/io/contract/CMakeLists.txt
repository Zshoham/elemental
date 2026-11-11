# The io contract test suite. Any el_reader or el_writer implementation, in
# this repository or outside it, links this and instantiates the suite with
# its own fixture.
elemental_add_contract_suite(io_contract
  SOURCES src/io_contract.cpp
  DEPENDS io)
