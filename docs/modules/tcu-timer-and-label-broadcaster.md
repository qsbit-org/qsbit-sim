# TCU Timer and Label Broadcaster

**Architecture position:** [Module map](../module-architecture.md#3-module-map).

## Responsibility and neighbors

One TCU rising-edge process owns deterministic time and invokes queue matching and launch preflight as logical substages.

| Direction | Contract |
| --- | --- |
| Upstream inputs | TCU clock, configured start tick, old timing-queue head, optional future pause port. |
| Downstream outputs | Reached label to every event queue, T_D and label-firing trace; future sync event output. |
| State owner and retained state | T_D, run or stopped state, last fired due cycle and pending interval. |

## Module diagram

```mermaid
flowchart LR
    U["TCU clock and timing head"] --> P["single edge timer step"]
    S[("T_D; start; interval")] <--> P
    P --> D["label broadcast and trace"]
    K["Activation: TCU rising edge"] -.-> P
```

## Behavior

**Activation:** Wake on TCU rising edge; an external configured start enables it independently of CPU progress.

**Transition:** On configured start edge S, set `T_D=0`. With TCU period P, edge `S+nP` has `T_D=n`. If a previously admitted timing-queue head is due at n, broadcast its label and check the whole expected event group before firing. Continue incrementing `T_D` through empty-queue gaps; a future synchronization profile may pause this local counter while global simulation time continues.

**Time and visibility:** No label is fired merely because an sc_event occurs; only the defined TCU edge controls this timer. A new group admitted on the same edge cannot fire.

**Reset and errors:** Baseline rejects pause and sync requests. Reset stops the timer and clears epoch state without rewinding SystemC time; overflow faults.

Cross-module timing and visibility follow the [baseline protocol](../module-architecture.md#4-baseline-protocol-and-event-ordering).

## Implementation and verification

TcuCycleModel computes logical cycles from the epoch start and broadcasts the due label. Empty gaps preserve cumulative time.

- Implementation: [tcu.cpp](../../src/tcu.cpp) and [tcu.hpp](../../include/qsbit/tcu.hpp).

**CTest:** `control.empty`, `systemc.tcu_trace`.

`control.empty` asserts cumulative launch ticks across empty gaps and late-admission rejection. `systemc.tcu_trace` checks that its clocked harness drains and writes the label trace.

- Numerical profile and supported scope: [Executable implementation](../implementation.md).
