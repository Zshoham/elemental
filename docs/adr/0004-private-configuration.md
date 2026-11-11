# 0004. Configuration is private to the implementation

Status: Accepted (2026-11-11)

## Context

Compile-time configuration is central to Elemental, and it is also the
easiest way to make a C library unsafe to consume. If a public header
changes with configuration, then every consumer's translation units must be
compiled with the library's exact configuration. A mismatch, a struct that
has a field in one and not the other, corrupts memory with no diagnostic.
It also forces every consumer to have the generated header, which means
running Kconfig or being handed its output.

## Decision

- The generated header, `elemental_config.h`, is a **private** include of
  Elemental's own sources. Public headers do not include it.
- **No public type, layout, function signature, or macro expansion depends
  on configuration.** Configuration changes which libraries exist and how
  they behave.
- One header is the stated exception: `elemental/assert.h`, which is for
  sources that implement Elemental modules and backends and refuses to
  compile unless the configuration was included first.
- Generated names carry the `EL_CONFIG_` prefix, so Elemental can sit
  inside a project that uses Kconfig with `CONFIG_`.
- An installed package ships its configuration (header, CMake variables,
  `.config`) for consumers that want to inspect it. Nothing requires them to.

The exception list is enforced: `tests/headers` compiles every public header
with no configuration, except those named in a list in that directory's
build file. Adding a name there is the act this record governs.

## Cost

- Public structs cannot drop fields to save RAM per configuration.
- A public macro or inline function cannot be configured. The first module
  to feel this is logging, where removing a call site at compile time needs
  the level in the caller's translation unit.
- Sources must include `elemental_config.h` first, by convention.

## Revisit when

A module shows a measured need for configuration in a consumer's
translation unit. It must then choose, in its proposal, between a compile
definition carried by its CMake target and a public generated header, and
it must make a mismatch a build or link error rather than a silent one. A
layout that varies with configuration additionally needs a link-time guard
that fails when headers and library disagree.
