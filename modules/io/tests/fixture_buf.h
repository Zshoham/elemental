#ifndef FIXTURE_BUF_H
#define FIXTURE_BUF_H

#include <elemental/io_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The memory reader (elemental/io_mem.h) and a fixed el_buf as a writer
 * (elemental/io_buf.h) under the contract suite. */
extern const el_io_contract_fixture io_fixture_buf;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_BUF_H */
