# Decoder transport and processing

`DecoderSystem` transfers measurement bits from controller memory-mapped I/O
(MMIO) registers to a decoding algorithm and returns correction bits. All cores
share the transport links and decoder processing capacity. Each core and decoder
ID pair has its own input window and accumulated corrections.

## Connections

- **Input:** aligned 32-bit data accesses from `MemoryModel`, routed through
  callbacks installed by `Simulator`.
- **Output:** status and correction registers read by the CPU; decoder trace
  events with core, decoder and request IDs.
- **Owner:** `Simulator::decoders_`.
- **Scheduling:** `Simulator::barrier()` calls `step()` after clocked memory
  accesses at that tick. `next_boundary()` supplies timed wakeups for transfers
  and jobs. A result returned at a memory edge becomes readable on a later edge.

See [decoder configuration and registers](../decoding.md) for the JSON fields,
MMIO commands and timing equations.

## Requests, jobs and results

`DecoderMmio` validates addresses, register permissions and command operands.
`DecoderSystem` accepts its commands, applies queue backpressure and advances
transfers, jobs and result state.

A submit write copies the core's staging registers into a bounded request
queue. A full queue keeps the memory write pending. Each request reserves
transmission time and incurs the configured propagation delay. Arriving packets
append their bits to the selected session's input window.

A complete window calls the configured decoder callback and creates a timed
job. The callback runs synchronously without advancing simulation time. The
job starts at least one nanosecond after admission and respects the decoder's
initiation interval. Its configured latency determines completion.

The response link sends completed jobs in completion order, breaking ties by
request ID. Returned bits accumulate by XOR until consumed or reset. The result
capacity limits sessions with an active job or unread corrections. A full result
capacity leaves the completing syndrome packet at the request queue head.

## Objects and state

| Member | Role |
| --- | --- |
| `mmio_` | `DecoderMmio` owns per-core registers and translates accesses into typed commands. |
| `sessions_` | Per-core, per-decoder partial input, active-job status and accumulated corrections. |
| `requests_`, `reset_request_` | Bounded syndrome queue and one separate reset slot. |
| `jobs_` | Decoded results with processing start, completion and response-arrival ticks. |
| `available_` | Earliest next job start for each decoder ID. |
| `tx_available_`, `rx_available_` | End of the reserved request and response transmission intervals. |

## Reset and errors

A decoder reset command travels over the request link using its separate slot.
On arrival it clears earlier work for the selected session, including its partial
window, queued syndrome requests, active job and unread corrections. Later
requests and other sessions remain. Reset takes precedence over completion and
result delivery at the same tick; existing transmission and job-start reservations
remain in effect.

`Simulator` reset clears all decoder sessions, queues, jobs and reservations.
`idle()` requires no pending request, reset, job or incomplete input window.
Unread completed corrections do not prevent simulation completion.

Construction rejects invalid capacities, timing values and decoder definitions.
Invalid MMIO accesses or decoder IDs fault. Input-window overflow, premature
result reads and consuming unfinished corrections also fault. A callback that
returns the wrong number of correction bits raises `BackendFailure`.

## Implementation and tests

Source: [decoder.hpp](../../include/qsbit/decoder.hpp),
[decoder.cpp](../../src/decoder.cpp), [decoder/mmio.cpp](../../src/decoder/mmio.cpp), [simulator.cpp](../../src/simulator.cpp)
and [decoding.py](../../python/qsbit_backend/decoding.py).

**CTest:** `decoder.transport`.

The tests cover transfer and processing times, queue backpressure, correction
accumulation, core routing, 64-bit payloads, reset ordering and invalid accesses.
Optional `decoder.pymatching` checks matrix configuration and correction masks.
Cross-project `integration.qec` runs compiled surface-code decoding and
repetition-code feedback programs.
