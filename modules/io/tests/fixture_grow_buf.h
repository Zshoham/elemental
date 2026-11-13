#ifndef FIXTURE_GROW_BUF_H
#define FIXTURE_GROW_BUF_H

#include <elemental/io_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* An el_grow_buf (elemental/io_alloc.h) as a writer under the contract suite. */
extern const el_io_contract_fixture io_alloc_fixture_grow_buf;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_GROW_BUF_H */
