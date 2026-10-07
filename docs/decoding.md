# Decoder feedback

The decoder receives measurement bits through memory-mapped I/O (MMIO)
registers and returns correction bits to the controller. A session is identified
by core ID and decoder ID. A run configuration specifies the
decoder algorithm, transfer bandwidth, link delay, processing latency and
queue capacities. The [QEC example](../examples/qec/README.md) compiles QIR
programs that use these registers.

**CTest:** `decoder.transport` checks bounded queues, transfer timing, result
routing, accumulated corrections, 64-bit payloads, reset under backpressure and
invalid accesses.
Optional `decoder.pymatching` checks matrix configuration and correction masks.
Cross-project `integration.qec` executes compiled measurement and correction
loops with decoder latency exceeding the original fixed timing budget.

## Configure a decoder

Install the `qec` extra and build with `QSBIT_PYTHON_BACKENDS=ON`.
Add a `decoding` object to the run file. All fields below are required.
`--backend-schema` includes its editor schema.
Decoder feedback runs execute the complete program; repeated-simulation
strategies are unsupported.

| Field | Meaning |
| --- | --- |
| `mmio_base` | Aligned start address of 32 bytes outside every core's RAM. |
| `request_capacity` | Maximum syndrome packets waiting for delivery or decoder admission. |
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
append bits in arrival order to the selected session.
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
Completion occurs `latency` later. An idle response link sends the completed
job with the earliest completion time, breaking ties by request ID. Transmission
is nonpreemptive. The next response can start when transmission ends, while the
previous response is still propagating. Different cores share these resources
but keep separate input windows and correction bits.

A full request queue holds syndrome-submit writes pending. Reset commands use
one separate slot shared by all cores and decoders. An occupied slot holds
further reset writes pending. Both kinds of request share the same transmission
bandwidth and propagation delay.

A full result queue holds the final packet of a window at the request queue
head. Reset bypasses that head when its packet arrives. Queue pressure does not
discard packets or replace unread results. Core memory edges run before
decoder progress at the same tick, so a result returned at that tick becomes
readable on a later memory edge.

Place `wait 0` before decoder polling to let the TCU pause when its queue
empties. CPU polling, request transport and decoder processing continue in
physical time. Submitted work resumes the TCU; the next positive-interval point
restores strict deadlines when it triggers. Without `wait 0`, a late codeword
still fails with `LateAdmission`.

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
| 20 | Command: 1 submits, 2 resets, 3 consumes corrections. | Status: bit 0 means corrections are ready; bit 1 means syndrome requests, reset, processing or an incomplete window remain. |
| 24 | Invalid. | Correction bits 0–31. |
| 28 | Invalid. | Correction bits 32–63. |

Command 1 snapshots the selected ID, count, payload and tag. The least
significant payload bit is the first measurement.

Command 2 travels through the request link. On arrival, it clears the selected
core's session on the selected decoder: partial input, earlier queued syndrome
requests, the active job and unread corrections. Later requests and other
sessions remain unchanged. Reset takes precedence over job completion and
result delivery at the same tick. A cancelled job cannot return corrections.
A response that has already started transmitting occupies the link until its
transmission ends. Request link and decoder start reservations remain intact.
Poll status bit 1 until it clears to wait for reset completion before submitting
new work.

Command 3 clears completed corrections locally. Consuming an unfinished result
or reading corrections before any result has returned is a protocol error.

A simulator reset clears all sessions, queued requests, the reset slot, jobs
and reservations. A pending decoder reset prevents a successful drain.
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
in `detail`; that field also reports syndrome request, pending reset and job
counts. Ticks are nanoseconds. Result consumption uses ID zero.
