#ifndef FIXTURE_DEFERRED_H
#define FIXTURE_DEFERRED_H

#include <elemental/log_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The deferred sink (elemental/log_deferred.h) under the contract suite,
 * drained into a memory sink. */
extern const el_log_contract_fixture log_fixture_deferred;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_DEFERRED_H */
