# 0001. Elemental replaces selected APIs; it is not a libc or a runtime

Status: Accepted (2026-11-11)

## Context

Elemental is described as a modern alternative to parts of libc and common
platform support libraries. "Alternative to libc" can be read as anything
from "better functions for a few jobs" to "a C library you can boot on".
The readings differ by orders of magnitude in work and in what must be
promised, and the larger ones cannot be walked back once users depend on
them.

## Decision

Elemental provides its own interfaces for selected jobs. It does not provide:

- a freestanding C runtime, startup code, or linker scripts;
- the functions a compiler requires of its environment (`memcpy`, `memset`,
  `memmove`, `memcmp`, `__aeabi_*`, soft-float and integer helpers);
- ISO C or POSIX compatibility, or symbols that replace libc's.

A program using Elemental links whatever C library its platform has, or
none. Each module states its runtime dependencies, and the freestanding
build checks them against an allowlist.

## Cost

A bare-metal user still needs a runtime from somewhere. Elemental's
freestanding story is "it needs very little", not "it is all you need".

## Revisit when

Several ports carry the same runtime shim. The evidence from the first slice
points the other way: its libraries need one external symbol, and everything
else the Cortex-M3 test image needed was demanded by the compiler or the
test, not by Elemental. If shims do accumulate, the answer is a separate,
optional, clearly named module, with this record superseded for that module
only.
