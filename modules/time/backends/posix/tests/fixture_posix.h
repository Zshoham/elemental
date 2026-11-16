#ifndef FIXTURE_POSIX_H
#define FIXTURE_POSIX_H

#include <elemental/time_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The POSIX clock (elemental/time_posix.h) under the contract suite. */
extern const el_time_contract_fixture time_fixture_posix;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_POSIX_H */
