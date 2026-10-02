.include "quantum.inc"
.global _start
.text
_start:
li t0, 8
wait.r t0
li t1, 0
li t2, 2
cw.r.r t1, t2
li t1, 3
li t2, 2
cw.r.r t1, t2
li t0, 4
wait.r t0
li t1, 0
li t2, 6
cw.r.r t1, t2
li t0, 4
wait.r t0
li t1, 0
li t2, 6
cw.r.r t1, t2
li t0, 4
wait.r t0
li t1, 0
li t2, 2
cw.r.r t1, t2
li t1, 3
li t2, 2
cw.r.r t1, t2
li t0, 4
wait.r t0
li t1, 0
li t2, 4
cw.r.r t1, t2
li t1, 3
li t2, 4
cw.r.r t1, t2
sim_exit
