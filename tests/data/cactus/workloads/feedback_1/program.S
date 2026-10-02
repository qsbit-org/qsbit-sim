.include "quantum.inc"
.global _start
.text
_start:
li t0, 8
wait.r t0
li t1, 0
li t2, 1
cw.r.r t1, t2
li t0, 4
wait.r t0
li t1, 0
li t2, 4
cw.r.r t1, t2
li t0, 40
wait.r t0
li t0, 30
wait.r t0
li t0, 20
wait.r t0
fmr a0, 0
li t0, 10
wait.r t0
li t1, 1
beq a0, x0, zero
li t2, 1
j done
zero:
li t2, 3
done:
cw.r.r t1, t2
sim_exit
