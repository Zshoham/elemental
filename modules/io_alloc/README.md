# io_alloc

`el_grow_writer`: an `el_writer` that collects what is written to it in
memory from an `el_allocator`, growing as needed.

| | |
|---|---|
| Stability | Experimental ([policy](../../docs/process/api-stability.md)) |
| Targets | `elemental::io_alloc` |
| Depends on | `io`, `alloc`. Runtime: `memcpy`. |
| Allocates | Through the allocator it is given |
| Kconfig | `IO_ALLOC` |

It is its own library so that `io` never depends on `alloc`: an application
that links only `elemental::io` builds no allocator, which
`integration.subdirectory` checks.

## What is here

| Piece | Kind | Header |
|---|---|---|
| `el_grow_writer` | Portable implementation of `el_writer` | [`elemental/io_alloc.h`](include/elemental/io_alloc.h) |

## Using it

```c
el_grow_writer body;
el_grow_writer_init(&body, allocator);
el_status status = el_write_all(el_grow_writer_as_writer(&body), data, len, NULL);
/* EL_ERR_NO_SPACE: the allocator would not let it grow. */
/* el_grow_writer_data(&body), el_grow_writer_len(&body) */
el_grow_writer_deinit(&body);
```

A write that finds the buffer full grows it to twice its capacity, or to
what the write needs if that is more, at least 16 bytes. If the allocator
refuses, it asks once more for exactly what the write needs, then takes
what fits. With no room at all it returns `EL_ERR_NO_SPACE`, which, unlike
`EL_ERR_END_OF_STREAM`, is not permanent: the same write can succeed once
the allocator has room. That status is one of the outcomes
[io.h](../io/include/elemental/io.h) lists for writers that allocate.

Growth allocates the new buffer before freeing the old one. Over a bump
arena a writer that has grown to n bytes has used up to about 2n.

## Tests

| Test | What it checks |
|---|---|
| `io_alloc.contract.*` | The io contract suite against the grow writer, over a stack arena wrapped in the checking allocator. |
| `io_alloc.unit.*` | Growth sizes, the exact-size retry, a partial write at the limit, `EL_ERR_NO_SPACE` and recovery from it; every buffer freed with the size it was allocated with. |
| `io_alloc.violations.*` | Each documented contract violation stops the program. Death tests, host only. |

In the io contract suite, 3 tests are skipped: the grow writer cannot stall,
and it does not end. The fixture's reader is a memory reader, because the
suite requires one; `io_alloc` has no reader of its own.
