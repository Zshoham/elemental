/*
 * elemental/log_contract.hpp - the log sink contract, as GoogleTest tests.
 *
 * Each test names one promise made in elemental/log.h. To run them against
 * a sink, describe how to set it up in a fixture
 * (elemental/log_contract.h) and instantiate the suite with it:
 *
 *     #include <elemental/log_contract.hpp>
 *
 *     #include "my_fixture.h"
 *
 *     INSTANTIATE_TEST_SUITE_P(MySink, ElLogContract, testing::Values(&my_fixture));
 *
 * Link elemental::log_contract and a GoogleTest main(). The tests appear as
 * MySink/ElLogContract.<Promise>/0. A test whose promise the fixture cannot
 * set up (an optional capability left null) is reported as skipped.
 *
 * This header needs C++17 and GoogleTest, which only tests do.
 */
#ifndef ELEMENTAL_LOG_CONTRACT_HPP
#define ELEMENTAL_LOG_CONTRACT_HPP

#include <gtest/gtest.h>

#include <iosfwd>

#include <elemental/log_contract.h>

/* The suite. The parameter is the fixture, which must outlive the test run. */
class ElLogContract : public testing::TestWithParam<const el_log_contract_fixture *>
{
  protected:
    /* Fails the test if the fixture lacks a required function. */
    void SetUp() override;
    /* Releases the last sink the test asked for. */
    void TearDown() override;
};

/* Failure reports name the fixture instead of printing its address. */
void PrintTo(const el_log_contract_fixture *fixture, std::ostream *os);

#endif /* ELEMENTAL_LOG_CONTRACT_HPP */
