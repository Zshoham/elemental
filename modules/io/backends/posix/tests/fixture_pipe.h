#ifndef FIXTURE_PIPE_H
#define FIXTURE_PIPE_H

#include <elemental/io_contract.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The POSIX backend (elemental/io_posix.h) under the contract suite. */
extern const el_io_contract_fixture io_posix_fixture_blocking_pipe;
extern const el_io_contract_fixture io_posix_fixture_nonblocking_pipe;

#ifdef __cplusplus
}
#endif

#endif /* FIXTURE_PIPE_H */
