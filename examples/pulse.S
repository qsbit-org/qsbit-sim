.include "quantum.inc"
.section .text
.global _start
_start:
  li t0, 8
  qadvance t0
  li t1, 0
  li t2, 5                      # Piecewise-constant X drive, pi rotation in 20 ns
  qappend x0, t1, t2
  li t0, 4
  qadvance t0
  li t2, 4
  qappend a0, t1, t2
  qread a1, a0
  la t3, signature
  sw a1, 0(t3)
  qend
.section .data
.global signature
signature: .word 0xffffffff
