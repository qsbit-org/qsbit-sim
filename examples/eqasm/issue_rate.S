.include "quantum.inc"
.global _start
.text
_start:
  .rept POINTS
    .if VLIW
      cw.bundle 0, 1, 2, 2
    .else
      cw.i.i 0, 1
      cw.i.i 2, 2
    .endif
    wait.i INTERVAL
  .endr
  sim_exit
