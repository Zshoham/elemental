/*
 * elemental/time_contract.hpp - the clock contract, as GoogleTest tests.
 *
 * Each test names one promise made in elemental/time.h. To run them against
 * a clock, describe how to set it up in a fixture
 * (elemental/time_contract.h) and instantiate the suite with it:
 *
 *     #include <elemental/time_contract.hpp>
 *
 *     #include "my_fixture.h"
 *
 *     INSTANTIATE_TEST_SUITE_P(MyClock, ElTimeContract, testing::Values(&my_fixture));
 *
 * Link elemental::time_contract and a GoogleTest main(). The tests appear
 * as MyClock/ElTimeContract.<Promise>/0. A test whose promise the fixture
 * cannot set up (the optional capability left null) is reported as skipped.
 *
 * This header needs C++17 and GoogleTest, which only tests do.
 */
#ifndef ELEMENTAL_TIME_CONTRACT_HPP
#define ELEMENTAL_TIME_CONTRACT_HPP

#include <gtest/gtest.h>

#include <iosfwd>

#include <elemental/time_contract.h>

/* The suite. The parameter is the fixture, which must outlive the test run. */
class ElTimeContract : public testing::TestWithParam<const el_time_contract_fixture *>
{
  protected:
    /* Fails the test if the fixture lacks a required function. */
    void SetUp() override;
    /* Releases the last clock the test asked for, if the fixture has a reset. */
    void TearDown() override;
};

/* Failure reports name the fixture instead of printing its address. */
void PrintTo(const el_time_contract_fixture *fixture, std::ostream *os);

#endif /* ELEMENTAL_TIME_CONTRACT_HPP */
