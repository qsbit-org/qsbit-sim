.include "quantum.inc"
.global _start
.text
_start:
li t0, 8
qadvance t0
li t1, 0
li t2, 3
qappend x0, t1, t2
li t0, 4
qadvance t0
li t1, 0
li t2, 4
qappend s0, t1, t2
li t0, 40
qadvance t0
li t0, 30
qadvance t0
li t0, 20
qadvance t0
qread a0, s0
li t0, 10
qadvance t0
li t1, 1
beq a0, x0, zero
li t2, 1
j done
zero:
li t2, 3
done:
qappend x0, t1, t2
qend
