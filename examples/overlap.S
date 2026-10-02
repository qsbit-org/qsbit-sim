# Two simultaneous drives on one qubit; run with overlap.json and the pulse backend.
.include "quantum.inc"
.global _start
.text
_start:
  li t0, 8
  wait.r t0
  li t1, 5
  cw.r.r x0, t1
  li t2, 1
  cw.r.r t2, t1
  sim_exit
