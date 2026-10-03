.include "quantum.inc"
.section .text
.global _start, allxy_round_begin, allxy_round_end
_start:
  la t0, repetitions
  lw s0, 0(t0)

.macro trial first, second
  wait.i 40000
  cw.i.i 0, \first
  wait.i 4
  cw.i.i 0, \second
  wait.i 4
  cw.i.i 0, 6
  fmr zero, 0
.endm
.macro pair first, second
  trial \first, \second
  trial \first, \second
.endm

allxy_round_begin:
  pair 1, 1  # II
  pair 2, 2  # XX
  pair 3, 3  # YY
  pair 2, 3  # XY
  pair 3, 2  # YX
  pair 4, 1  # xI
  pair 5, 1  # yI
  pair 4, 5  # xy
  pair 5, 4  # yx
  pair 4, 3  # xY
  pair 5, 2  # yX
  pair 2, 5  # Xy
  pair 3, 4  # Yx
  pair 4, 2  # xX
  pair 2, 4  # Xx
  pair 5, 3  # yY
  pair 3, 5  # Yy
  pair 2, 1  # XI
  pair 3, 1  # YI
  pair 4, 4  # xx
  pair 5, 5  # yy
allxy_round_end:
  addi s0, s0, -1
  bnez s0, allxy_round_begin
  sim_exit

.section .data
.global repetitions
repetitions: .word 1
