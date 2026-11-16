#ifndef FIXTURE_WRITER_H
#define FIXTURE_WRITER_H

#include <elemental/log_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The writer sink (elemental/log_writer.h) under the contract suite, with
 * an el_buf as its stream.
 */
extern const el_log_contract_fixture log_fixture_writer;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_WRITER_H */
