.include "quantum.inc"
.global _start
.text
_start:
  wait.i 10000
  .if VLIW
    cw.bundle 0, 3, 2, 3
  .else
    cw.i.i 0, 3
    cw.i.i 2, 3
  .endif
  wait.i 1
  .if VLIW
    cw.bundle 0, 1, 2, 2
  .else
    cw.i.i 0, 1
    cw.i.i 2, 2
  .endif
  wait.i 1
  .if VLIW
    cw.bundle 0, 4, 2, 4
  .else
    cw.i.i 0, 4
    cw.i.i 2, 4
  .endif
  wait.i 50
  sim_exit
