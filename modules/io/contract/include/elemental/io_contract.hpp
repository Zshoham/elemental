/*
 * elemental/io_contract.hpp - the io interface contract, as GoogleTest tests.
 *
 * Each test names one promise made in elemental/io.h. To run them against an
 * implementation, describe how to set it up in a fixture
 * (elemental/io_contract.h) and instantiate the suite with it:
 *
 *     #include <elemental/io_contract.hpp>
 *
 *     #include "my_fixture.h"
 *
 *     INSTANTIATE_TEST_SUITE_P(MyBackend, ElIoContract, testing::Values(&my_fixture));
 *
 * Link elemental::io_contract and a GoogleTest main(). The tests appear as
 * MyBackend/ElIoContract.<Promise>/0. A test whose promise the fixture cannot
 * set up (an optional capability left null) is reported as skipped.
 *
 * This is the one C++ header in the io module. It needs C++17 and
 * GoogleTest, which only tests do.
 */
#ifndef ELEMENTAL_IO_CONTRACT_HPP
#define ELEMENTAL_IO_CONTRACT_HPP

#include <gtest/gtest.h>

#include <iosfwd>

#include <elemental/io_contract.h>

/* The suite. The parameter is the fixture, which must outlive the test run. */
class ElIoContract : public testing::TestWithParam<const el_io_contract_fixture *>
{
  protected:
    /* Fails the test if the fixture lacks a required function. */
    void SetUp() override;
    /* Releases the last stream the test asked for. */
    void TearDown() override;
};

/* Failure reports name the fixture instead of printing its address. */
void PrintTo(const el_io_contract_fixture *fixture, std::ostream *os);

#endif /* ELEMENTAL_IO_CONTRACT_HPP */
