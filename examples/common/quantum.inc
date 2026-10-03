.option norvc
.option norelax
.macro cw.r.r port, codeword
  .insn r 0x0b, 0, 0, x0, \port, \codeword
.endm
.macro cw.i.r port, codeword
  .insn r 0x0b, 0, 1, x0, x\port, \codeword
.endm
.macro cw.r.i port, codeword
  .insn r 0x0b, 0, 2, x0, \port, x\codeword
.endm
.macro cw.i.i port, codeword
  .insn r 0x0b, 0, 3, x0, x\port, x\codeword
.endm
# Each mode uses bit 0 for an immediate port and bit 1 for an immediate codeword.
# Register operands use register numbers.
.macro cw.bundle port0, codeword0, port1, codeword1, mode0=3, mode1=3
  .if (\port0 < 0) || (\port0 > 31) || (\port1 < 0) || (\port1 > 31)
    .error "cw.bundle port operand exceeds 5 bits"
  .endif
  .if (\codeword0 < 0) || (\codeword0 > 31) || (\codeword1 < 0) || (\codeword1 > 31)
    .error "cw.bundle codeword operand exceeds 5 bits"
  .endif
  .if (\mode0 < 0) || (\mode0 > 3) || (\mode1 < 0) || (\mode1 > 3)
    .error "cw.bundle mode exceeds 2 bits"
  .endif
  .word 0x2b | ((\port0 | (\codeword0 << 5) | (\mode0 << 10)) << 7) | ((\port1 | (\codeword1 << 5) | (\mode1 << 10)) << 19)
.endm
.macro wait.r interval
  .insn r 0x0b, 1, 0, x0, \interval, x0
.endm
.macro wait.i interval
  .if (\interval < 0) || (\interval > 131071)
    .error "wait.i interval exceeds 17 bits"
  .endif
  .word (\interval << 15) | 0x200b
.endm
.macro fmr rd, target
  .insn r 0x0b, 3, 0, \rd, x\target, x0
.endm
.macro sync target
  .if (\target < 0) || (\target > 131071)
    .error "sync target exceeds 17 bits"
  .endif
  .word (\target << 15) | 0x600b
.endm
.macro sim_exit
  li a0, 0
  li a7, 93
  ecall
.endm
