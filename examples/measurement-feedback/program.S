.include "quantum.inc"
.section .text
.global _start
_start:
  li t0, 8
  wait.r t0
  li t1, 0
  li t2, 1                      # Prepare qubit 0 in |1>
  cw.r.r t1, t2
  li t0, 4
  wait.r t0
  li t2, 4
  cw.r.r t1, t2
  fmr a1, 0
  beq a1, x0, zero_result
  li t2, 1                      # X on qubit 1 for a measured one
  jal x0, selected
zero_result:
  li t2, 3                      # Z on qubit 1 for a measured zero
selected:
  li t0, 14                     # Schedule at TCU cycle 26
  wait.r t0
  li t1, 1
  cw.r.r t1, t2
  la t3, signature
  sw a1, 0(t3)
  sim_exit
.section .data
.global signature
signature: .word 0xffffffff
