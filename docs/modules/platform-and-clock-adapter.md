# Platform Configuration and Clock Adapter

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

Platform setup owns the immutable timing profile; a session coordinator owns reset requests. This is a SystemC integration boundary, not a per-cycle hardware datapath.

| Direction | Contract |
| --- | --- |
| Upstream inputs | CLI configuration, the device profile, selected ISA profile, and a requested session start or reset tick. |
| Downstream outputs | CPU and TCU clocks, cross-domain mailbox capacities and latencies, DeviceRuntime timing, epoch and trace metadata. |
| State owner and retained state | Validated integer tick resolution, clock periods and phases, start tick, current session epoch, and immutable configuration hash. |

## Module diagram

```{graphviz}
digraph module {
  rankdir=TB; bgcolor="transparent";
  node [shape=box, style="rounded,filled", fillcolor="#edf6f7", color="#43818a", fontname="sans-serif", fontsize=11];
  input [label="Validated profile and reset request"];
  owner [label="Simulator"];
  state [label="profile_; cpu_clock_, tcu_clock_; wake_, barrier_"];
  output [label="Clock edges / reset / physical wakeup"];
  input -> owner [label="input / call"]; owner -> output [label="result / effect"]; state -> owner [style=dashed, label="owned state / configuration"];
}
```

## Behavior

**Activation:** Validate and construct before elaboration. Each edge callback and timed wakeup checks reset before ordinary work; the first callback applies it once for that tick. Clock edges wake their respective domain owners.

**Transition:** Check that every period, phase and device delay maps exactly to global ticks; create separate CPU and TCU clocks; publish a single read-only profile snapshot to all owners. Configure start independently of CPU progress so admission backpressure cannot prevent timer start.

**Time and visibility:** Global simulation time never rewinds. At a coincident reset and clock edge, reset dominates and suppresses that edge’s ordinary state transition. Ordinary clocks still create every modeled edge.

**Reset and errors:** Reject unrepresentable or inconsistent parameters before sc_start. Session reset preserves the immutable timing profile, increments the epoch, invalidates old callbacks and initializes the backend; a future controller-only reset needs a different contract.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Objects and state transition

| Object or member | Representation | Role |
| --- | --- | --- |
| `profile_` | `const Profile` | Validated clocks, capacities, mappings and delays; immutable across reset. |
| `cpu_clock_, tcu_clock_` | `sc_clock` | CPU/memory and TCU rising-edge activation. |
| `wake_, barrier_` | `sc_event` | Timed wakeup and zero-time barrier activation; payload lives in owners. |
| `cpu_done_, memory_done_, tcu_done_` | `optional tick` | Marks which coincident edge transitions have completed. |
| `epoch_, resets_, last_reset_` | `session state` | Applies a reset once per requested tick and invalidates old work. |

On an edge, reset is applied first. Otherwise the owning model advances and records its done tick. The barrier waits for all clock edges due at that tick, then processes a due device boundary. schedule_wakeup selects the next physical boundary, reset or watchdog; it cancels the prior wake notification before scheduling another.

[Current C++ declarations](../api.md#simulatorhpp).

## Implementation and verification

Simulator owns immutable configuration, three clock-edge methods, timed wakeups and the explicit device barrier.

- Implementation: [simulator.cpp](../../src/simulator.cpp) and [simulator.hpp](../../include/qsbit/simulator.hpp).

**CTest:** `systemc.use_cases`.

Checks unequal periods/phases, reset at coincident physical boundaries and exact trace equality under reversed process registration.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
