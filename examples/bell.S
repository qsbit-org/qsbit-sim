.include "quantum.inc"
.section .text
.global _start
_start:
  li t0, 8
  qadvance t0
  li t1, 0
  li t2, 2                      # H on qubit 0
  qappend x0, t1, t2
  li t0, 2
  qadvance t0
  li t2, 6                      # CX on qubits 0 and 1
  qappend x0, t1, t2
  qadvance t0
  li t2, 4                      # Acquire both qubits at one timing point
  qappend a0, t1, t2
  li t1, 1
  qappend a1, t1, t2
  qread a2, a0
  qread a3, a1
  la t3, signature
  sw a2, 0(t3)
  sw a3, 4(t3)
  qend
.section .data
.global signature
signature: .word 0xffffffff, 0xffffffff
