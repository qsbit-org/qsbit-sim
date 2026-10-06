# Decoder feedback

The decoder receives measurement bits through memory-mapped registers and
returns correction bits to the controller. A run configuration specifies the
decoder algorithm, transfer bandwidth, link delay, processing latency and
queue capacities. The [QEC example](../examples/qec/README.md) compiles QIR
programs that use these registers.

**CTest:** `decoder.transport` checks bounded queues, transfer timing, result
routing, accumulated corrections, 64-bit payloads, reset and invalid accesses.
Optional `decoder.pymatching` checks matrix configuration and correction masks.
Cross-project `integration.qec` executes compiled measurement and correction
loops and checks late feedback failure.

## Configure a decoder

Install the `qec` extra and build with `QSBIT_PYTHON_BACKENDS=ON`.
Add a `decoding` object to the run file. All fields below are required.
`--backend-schema` includes its editor schema.
Decoder feedback runs execute the complete program; repeated-simulation
strategies are unsupported.

| Field | Meaning |
| --- | --- |
| `mmio_base` | Aligned start address of 32 bytes outside every core's RAM. |
| `request_capacity` | Maximum packets waiting for delivery or decoder admission. |
| `result_capacity` | Maximum sessions with an active job or unread corrections. |
| `link_latency` | Positive one-way propagation delay in nanoseconds. |
| `bytes_per_tick` | Positive transfer bandwidth in bytes per nanosecond. |
| `packet_overhead` | Header bytes per request and response; zero is allowed. |
| `decoders` | Nonempty array of decoder configurations. |

Each decoder has a unique 32-bit `id`, `measurements` from 1 to 1048576,
`outputs` from 1 to 64, positive `latency` and `initiation_interval` in
nanoseconds, a `backend` name and an `options` object. Transport capacities,
bandwidth and timing values are positive 32-bit integers. IDs, overhead and
the base address may be zero.

`measurements` is the number of input bits in one decoding window. Packets
append bits in arrival order to the session identified by core and decoder ID.
A complete window starts one decoding job. Another window for the same session
waits until that job returns. Returned correction bits accumulate by XOR until
the CPU consumes them or resets the decoder.

### PyMatching

Set `backend` to `pymatching`. Its options are:

| Option | Shape and meaning |
| --- | --- |
| `check_matrix` | Binary matrix with one row per detector and one column per error mechanism. Each column has at most two nonzero entries. |
| `observables` | Binary matrix with `outputs` rows and one column per error mechanism; maps a correction to returned bits. |
| `measurement_to_detector` | Binary matrix with one row per detector and `measurements` columns. |
| `weights` | One finite matching weight per error mechanism. |

The adapter computes detector bits by multiplying `measurement_to_detector`
by the input vector modulo two, then calls PyMatching. The `observables`
matrix determines whether the returned bits represent physical corrections
or logical observable flips. See the repetition-code target in the
[compiler examples](https://github.com/qsbit-org/qsbit-compiler/tree/main/examples/qec).

### Custom algorithms

Set `backend` to `module:factory`. The factory receives the decoder configuration
and returns a callable accepting a list of `measurements` binary values. It
returns a list of `outputs` binary values. Each call decodes a complete window
independently. Use the run file's `python_path` to locate an external module.

The algorithm runs synchronously when a complete window is admitted. Its host
execution time does not advance simulation time. The configured processing
and transport delays determine when the CPU can observe its result.

## Timing and capacity

Each direction has one shared FIFO link. A packet occupies that link for
`max(1, ceil((packet_overhead + payload_bytes) / bytes_per_tick))` nanoseconds,
then incurs `link_latency`. Syndrome payloads use `ceil(count / 8)` bytes;
responses use `ceil(outputs / 8)` bytes; reset packets have no payload.

The decoder starts a job no earlier than one nanosecond after admission and no
earlier than `initiation_interval` after the previous start on that decoder ID.
Completion occurs `latency` later. Response link reservations follow job
admission order. Different cores share these resources but keep separate input
windows and correction bits.

A full request queue holds the CPU's memory write pending. A full result queue
holds the final packet of a window at the request queue head. The simulator
does not drop packets or replace unread results. Core memory edges run before
decoder progress at the same tick, so a result returned at that tick becomes
readable on a later memory edge.

Quantum operation deadlines remain the TCU's scheduled time points. Waiting
for a decoder consumes CPU time without advancing those time points. A late
codeword fails with `LateAdmission`.

## Registers

Each core has separate staging registers at the same MMIO address. Accesses
must be aligned 32-bit data loads or stores.

| Byte offset | Write | Read |
| --- | --- | --- |
| 0 | Select decoder ID. | Invalid. |
| 4 | Set packet bit count. | Selected decoder's output width. |
| 8 | Set payload bits 0–31. | Invalid. |
| 12 | Set payload bits 32–63. | Invalid. |
| 16 | Set 32-bit logging tag. | Invalid. |
| 20 | Command: 1 submits, 2 resets, 3 consumes corrections. | Status: bit 0 means corrections are ready; bit 1 means requests, processing or an incomplete window remain. |
| 24 | Invalid. | Correction bits 0–31. |
| 28 | Invalid. | Correction bits 32–63. |

Command 1 snapshots the selected ID, count, payload and tag. The least
significant payload bit is the first measurement. Command 2 travels through
the request link and clears that session's input, active job and corrections
when it arrives. It leaves existing link and decoder start reservations intact.
Command 3 clears completed corrections locally. Consuming an unfinished result
or reading corrections before any result has returned is a protocol error.

A simulator reset clears all sessions, queued requests, jobs and reservations.
An incomplete window prevents a successful drain; the watchdog terminates a
program that exits without completing it. An unread completed result does not
prevent drain.

## Trace

`DecoderRequestSubmitted`, `DecoderRequestSent` and `DecoderRequestArrived`
record the request path. `DecoderStarted`, `DecoderCompleted` and
`DecoderResultReturned` record processing and the response path. A job uses
the ID of the packet that completes its measurement window.
`DecoderReset` and `DecoderResultConsumed` record result lifetime changes.

These records use `core` for the requesting controller, `value` for decoder ID,
and `id` for request or job identity. Request records include the logging tag
in `detail`; that field also reports request and job counts. Ticks are
nanoseconds. Result consumption uses ID zero.
