.include "quantum.inc"
.section .text
.global _start
_start:
  li t0, 8
  qadvance t0
  li t1, 0
  li t2, 1                      # Prepare qubit 0 in |1>
  qappend x0, t1, t2
  li t0, 4
  qadvance t0
  li t2, 4
  qappend a0, t1, t2
  qread a1, a0                  # Result must return through the modeled CPU crossing
  beq a1, x0, zero_result
  li t2, 1                      # X on qubit 1 for a measured one
  jal x0, selected
zero_result:
  li t2, 3                      # Z on qubit 1 for a measured zero
selected:
  li t0, 116                    # Planned logical cycle 128, independent of branch duration
  qadvance t0
  li t1, 1
  qappend x0, t1, t2
  la t3, signature
  sw a1, 0(t3)
  qend
.section .data
.global signature
signature: .word 0xffffffff
