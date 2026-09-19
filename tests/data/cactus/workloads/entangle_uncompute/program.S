.include "quantum.inc"
.global _start
.text
_start:
li t0, 8
qadvance t0
li t1, 0
li t2, 2
qappend x0, t1, t2
li t1, 3
li t2, 2
qappend x0, t1, t2
li t0, 4
qadvance t0
li t1, 0
li t2, 6
qappend x0, t1, t2
li t0, 4
qadvance t0
li t1, 0
li t2, 6
qappend x0, t1, t2
li t0, 4
qadvance t0
li t1, 0
li t2, 2
qappend x0, t1, t2
li t1, 3
li t2, 2
qappend x0, t1, t2
li t0, 4
qadvance t0
li t1, 0
li t2, 4
qappend s0, t1, t2
li t1, 3
li t2, 4
qappend s1, t1, t2
qend
