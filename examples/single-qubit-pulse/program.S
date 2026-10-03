.include "quantum.inc"
.section .text
.global _start
_start:
  li t0, 8
  wait.r t0
  li t1, 0
  li t2, 5                      # Piecewise-constant X drive, pi rotation in 20 ns
  cw.r.r t1, t2
  li t0, 4
  wait.r t0
  li t2, 4
  cw.r.r t1, t2
  fmr a1, 0
  la t3, signature
  sw a1, 0(t3)
  sim_exit
.section .data
.global signature
signature: .word 0xffffffff
