# The log sink contract test suite. Any el_log_sink implementation, in this
# repository or outside it, links this and instantiates the suite with its
# own fixture.
elemental_add_contract_suite(log_contract
  SOURCES src/log_contract.cpp
  DEPENDS log)
