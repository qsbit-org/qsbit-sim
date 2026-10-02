.include "quantum.inc"
.section .text
.global _start, repeat_begin, repeat_end
_start:
  la t0, repetitions
  lw s0, 0(t0)
repeat_begin:
  wait.i 400
  cw.i.i 0, 1
  wait.i 4
  cw.i.i 0, 2
  fmr zero, 0
repeat_end:
  addi s0, s0, -1
  bnez s0, repeat_begin
  sim_exit
.section .data
.global repetitions
repetitions: .word 1
