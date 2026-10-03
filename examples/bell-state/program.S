.include "quantum.inc"
.section .text
.global _start
_start:
  li t0, 8
  wait.r t0
  li t1, 0
  li t2, 2                      # H on qubit 0
  cw.r.r t1, t2
  li t0, 2
  wait.r t0
  li t2, 6                      # First CX output
  cw.r.r t1, t2
  cw.i.i 1, 10                  # Second CX output at the same time point
  wait.r t0
  li t2, 4                      # Acquire both qubits at one timing point
  cw.r.r t1, t2
  li t1, 1
  cw.r.r t1, t2
  fmr a2, 0
  fmr a3, 1
  la t3, signature
  sw a2, 0(t3)
  sw a3, 4(t3)
  sim_exit
.section .data
.global signature
signature: .word 0xffffffff, 0xffffffff
