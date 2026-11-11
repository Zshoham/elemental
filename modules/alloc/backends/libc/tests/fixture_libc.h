#ifndef FIXTURE_LIBC_H
#define FIXTURE_LIBC_H

#include <elemental/alloc_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The C library heap (elemental/alloc_libc.h) under the contract suite. */
extern const el_alloc_contract_fixture alloc_fixture_libc;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_LIBC_H */
