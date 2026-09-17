# ELF Loader and Program Image

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Pure C++ startup component. It has no sensitivity list, `wait()`, or runtime SystemC process.

| Direction | Contract |
| --- | --- |
| Upstream inputs | RV32 ELF or test-only raw image, memory map and optional extension manifest. |
| Downstream outputs | Initialized memory bytes, executable ranges, initial PC and load diagnostics. |
| State owner and retained state | Load plan and checked segment descriptors exist only during initialization; the memory model owns bytes afterward. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="ELF or raw image"];
  owner [label="ProgramImage"];
  state [label="bytes_; segments_; base_, entry_"];
  output [label="Validated ProgramImage and entry PC"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Call once before simulation or during an explicitly configured cold-start action.

**Transition:** Validate ELF class, machine, endianness, segment bounds, overlaps and entry PC; map loadable bytes and zero-fill required memory; publish the entry PC. For raw input require an explicit base address. The optional manifest can reject a known incompatible extension but cannot replace decoder checks.

**Time and visibility:** Program loading consumes no simulated cycles in the baseline. A modeled boot loader would be a separate timing profile.

**Reset and errors:** Reject a malformed image without partially starting simulation. Baseline session reset preserves loaded memory and reuses entry PC; a cold start reloads it.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `bytes_` | `vector<uint8_t>` | RAM bytes, including loaded segments and zero-filled BSS. |
| `segments_` | `vector<Segment>` | Mapped ranges and read/write/execute permissions. |
| `base_, entry_` | `32-bit addresses` | Memory base and initial CPU PC. |

elf validates headers, load ranges, permissions and entry before returning a complete image. raw loads uncompressed machine words at the explicit address. Runtime reads and writes check alignment, range and permissions. Loading happens before elaborated execution; MemoryModel later owns and updates the image.

[Current C++ declarations](../api.md#imagehpp).

## Implementation and verification

ProgramImage checks ELF32 segments and permissions; the memory owner retains its bytes.

- Implementation: [image.cpp](../../src/image.cpp) and [image.hpp](../../include/qsbit/image.hpp).

**CTest:** `core.image`, `systemc.use_cases`.

Checks image validation and permissions, raw input, and missing-file diagnostics.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
