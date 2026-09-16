# Quantum extension v1; assembled by GNU as, never parsed by the simulator.
.option norvc
.option norelax
.macro qappend rd, port, codeword
  .insn r 0x0b, 0, 0, \rd, \port, \codeword
.endm
.macro qadvance interval
  .insn r 0x0b, 1, 0, x0, \interval, x0
.endm
.macro qflush
  .insn r 0x0b, 2, 0, x0, x0, x0
.endm
.macro qread rd, token
  .insn r 0x0b, 3, 0, \rd, \token, x0
.endm
.macro qend
  .insn r 0x0b, 4, 0, x0, x0, x0
.endm
.macro qappend_if token, port, codeword, expected
  .insn r 0x0b, 5, \expected, \token, \port, \codeword
.endm
