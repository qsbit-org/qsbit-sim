.include "quantum.inc"
.global _start
.text
_start:
  li x3, REPETITIONS * 3
loop:
  wait.i 2
  sync 1
  wait.i 6
  wait.i 57
  cw.i.i 5, 1
  addi x3, x3, -1
  bnez x3, loop
  sim_exit
