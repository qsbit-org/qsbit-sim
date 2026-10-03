# Distributed simulation

Run multiple controller programs in one SystemC simulation. Each `Core` owns
its CPU, memory, timing control, TCU, measurement registers and synchronization
unit. `Simulator` owns the connections and a shared quantum device.

## Configure cores

Replace the top-level `program` field with `cores`. Each entry requires an
`id` and ELF `program` path. Core IDs are unique unsigned 17-bit integers.
Paths are relative to the run file.

```json
{
  "schema": 1,
  "backend": "mock",
  "profile": {
    "tcu": {"period": 4, "phase": 0},
    "start": 10000,
    "mappings": [],
    "two_qubit_gates": []
  },
  "cores": [
    {"id": 1, "program": "control.elf", "profile_file": "control-profile.json"},
    {"id": 2, "program": "readout.elf", "profile_file": "readout-profile.json"}
  ],
  "sync_connections": [
    {"first": 1, "second": 2, "first_to_second": 8, "second_to_first": 6, "capacity": 8}
  ],
  "trace": "trace.jsonl",
  "summary": "summary.json"
}
```

Each core inherits the top-level profile and CPU model. Its `profile_file`
and inline `profile` apply in that order. `cpu_model` selects `rv32` or `vliw`;
`sync_capacity` bounds queued synchronization events in the TCU and defaults
to 8. Profiles are validated after each overlay. Set `mappings` to an empty
array when a common profile changes the port count without defining mappings.

All cores share the TCU period and phase, qubit count, random seed and watchdog.
They also share the `two_qubit_gates` definitions.
CPU clocks and initial TCU start times may differ. A core configuration cannot
be combined with a top-level program, raw binary input, memory inspection or
memory dump. Use [the runnable example](../examples/distributed-hisq/README.md)
to generate complete input files.

## Neighbor connections

`first_to_second` and `second_to_first` are positive, calibrated delays in
TCU cycles. `capacity` bounds unconsumed signals in each direction and defaults
to 8. Self-connections, duplicate connections and unknown core IDs are invalid.
`sync target` requires a direct connection to that target.

Synchronization signals carry no quantum operations or measurement results.
Each received signal satisfies one request, in order. Signals arriving before
the matching local request remain buffered. Capacity released on an edge
becomes available on the following edge.

See [synchronization](modules/synchronization.md) for BISP timing.

## Shared quantum device

Qubit targets and resource IDs identify objects in the shared device. Output
ports are local to each core; the simulator assigns disjoint device-port ranges
in core-array order. Device trace records use these global output port numbers.

A two-qubit gate can require one codeword from each core. Its `inputs` identify
core IDs and local ports; the two `gate_output` actions must reach the same
physical start tick after their output delays. The shared device applies the
gate once. BISP aligns controller timing; it does not carry or assemble gate
operations. See [two-port gate outputs](interfaces.md#two-port-gate-outputs).

All cores submit operations before the device processes a physical boundary.
The backend evolves once for each interval with the active drives from every
core. A measurement result returns to the registers of the core that requested
it. Numerical backend selection and noise options use the existing
[backend configuration](backends.md).

Device event IDs and measurement IDs are globally unique. For N cores,
zero-based core index i and local ID k, the device ID is `(k - 1) * N + i + 1`.
Mock `outcomes` uses device measurement IDs.

## Completion, reset and traces

A halted CPU does not stop other cores. Successful completion requires every
core, the shared device and all synchronization connections to drain.
The backend advances to the final stop tick before the run finishes.
An unmatched synchronization reaches the configured watchdog and fails.

Session reset clears all cores, synchronization signals and the shared quantum
state. Independent core resets are unsupported.

Multicore trace records include `core` where an event belongs to one controller.
`SyncBooked.value` is the local countdown deadline; `SyncReceived.value` is the
signal's arrival tick. `SyncCompleted` records satisfaction of both conditions.
`TimerPaused` and `TimerResumed` record changes to the TCU timer state.
Global completion and reset records have no core ID.
The summary contains per-core CPU registers, measurement registers, profiles
and drain status under `cores`, plus the configured `sync_connections`.

## Supported scope

The model implements neighbor BISP with fixed link delays and phase-aligned
TCU clocks. Regional synchronization through routers, `send`, `recv`, clock
drift and link failures are unsupported.

**CTest:** `sync.neighbor`, `systemc.distributed`.

The unit test checks countdown timing, early signals, repeated synchronization,
capacity and reset. The integration test checks result routing, shared resources,
global completion, unmatched requests and registration-order independence.
The optional Aer test `numerical.distributed` prepares a qubit from one core and
measures it from another.
It also checks a Bell state prepared by a two-port CX across cores, thermal
relaxation during a TCU pause and evolution through the global stop tick.
