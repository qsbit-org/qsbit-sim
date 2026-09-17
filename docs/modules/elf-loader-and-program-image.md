# ELF Loader and Program Image

`ProgramImage` loads an RV32 program into simulated memory and records its
entry address and segment permissions. The memory model takes ownership of this
image before the CPU starts fetching instructions.

## Connections

- **Input:** ELF file bytes, RAM base and size; raw input also supplies a load address.
- **Output:** initialized RAM, mapped segments and the initial PC for `MemoryModel`
  and the CPU.
- **Scheduling:** loading runs once during application setup and consumes no
  simulated time.

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="ELF or raw image"];
  owner [label="ProgramImage"];
  state [label="bytes_\nsegments_\nbase_, entry_"];
  output [label="Validated ProgramImage and entry PC"];
  input -> owner; owner -> output; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Loading and access checks

`ProgramImage::elf()` checks that the file is a little-endian ELF32 RISC-V
executable. It validates loadable segment bounds, overlaps, permissions and the
aligned executable entry. It copies segment data into RAM and zero-fills BSS.

`ProgramImage::raw()` loads a nonempty sequence of 32-bit machine words at the
specified address. Assembly text must be assembled and linked before loading.

During execution, reads and writes check address range, alignment and segment
permissions. Unmapped bytes within configured RAM may hold data, but instruction
fetches must address executable segments. See [program input](../interfaces.md#program-input)
for the supported file format.

## Objects and state

| Object or member | Representation | Role |
| --- | --- | --- |
| `bytes_` | `vector<uint8_t>` | RAM bytes, including loaded segments and zero-filled BSS. |
| `segments_` | `vector<Segment>` | Mapped ranges and read/write/execute permissions. |
| `base_, entry_` | 32-bit addresses | Memory base and initial CPU PC. |

[C++ API](../api.md#imagehpp).

## Reset and errors

A malformed image fails before simulation starts. Misaligned, out-of-range
or disallowed runtime accesses return typed faults through the memory model.
Session reset restores the CPU entry PC and preserves all memory bytes, including
stores already completed. Reload the image to start with its original contents.

## Implementation and tests

Source: [image.cpp](../../src/image.cpp) and [image.hpp](../../include/qsbit/image.hpp).

**CTest:** `core.image`, `systemc.use_cases`.

The tests reject malformed images and disallowed accesses, load raw programs,
and check diagnostics for missing files.
