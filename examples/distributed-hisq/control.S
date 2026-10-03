.include "quantum.inc"
.global _start
.text
_start:
  li x2, 120
  li x3, REPETITIONS
outer:
  li x1, 0
  wait.i 1
  cw.i.i 21, 2
inner:
  addi x1, x1, 40
  cw.i.i 20, 2
  wait.r x1
  sync 2
  wait.i 8
  cw.i.i 7, 1
  wait.i 50
  bne x1, x2, inner
  addi x3, x3, -1
  bnez x3, outer
  sim_exit
