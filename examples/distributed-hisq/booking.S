.include "quantum.inc"
.global _start
.text
_start:
  wait.i READY
  cw.i.i 0, 1
  .if DURATION > LATENCY
    wait.i (DURATION - LATENCY)
  .endif
  sync PEER
  wait.i LATENCY
  cw.i.i 1, 1
  sim_exit
