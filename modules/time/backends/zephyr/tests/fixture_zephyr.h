#ifndef FIXTURE_ZEPHYR_H
#define FIXTURE_ZEPHYR_H

#include <elemental/time_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The Zephyr clock (elemental/time_zephyr.h) under the contract suite. */
extern const el_time_contract_fixture time_fixture_zephyr;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_ZEPHYR_H */
