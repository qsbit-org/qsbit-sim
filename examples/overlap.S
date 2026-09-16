# Two simultaneous drives on one qubit; run with overlap.json and the pulse backend.
.include "quantum.inc"
.global _start
.text
_start:
  li t0, 8
  qadvance t0
  li t1, 5
  qappend x0, x0, t1
  li t2, 1
  qappend x0, t2, t1
  qend
