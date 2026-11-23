#ifndef FIXTURE_RING_H
#define FIXTURE_RING_H

#include <elemental/io_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The two sides of an el_ring (elemental/ring.h) under the io contract
 * suite: a ring whose producer closed it after the content, which ends. */
extern const el_io_contract_fixture ring_fixture;

/* The reader of a ring that is not closed after its content: it stalls
 * instead of ending. Reader half only. */
extern const el_io_contract_fixture ring_fixture_open;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_RING_H */
