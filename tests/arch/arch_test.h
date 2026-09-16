// Keep upstream test-body and signature macros; replace only platform entry/exit.
#include_next "arch_test.h"
// The upstream LA helper temporarily enables C for padding. This platform is
// IALIGN=32: keep the same alignment and address load using RV32I NOP padding.
#undef LA
#define LA(reg, val) ; .option push; .option norvc; .align UNROLLSZ; la reg,val; .align UNROLLSZ; .option pop
.purgem RVTEST_CODE_BEGIN
.macro RVTEST_CODE_BEGIN
  .option norvc
  .section .text.init
  .global rvtest_init
  .global rvtest_code_begin
rvtest_init:
  RVTEST_INIT_GPRS
rvtest_code_begin:
.endm
.purgem RVTEST_CODE_END
.macro RVTEST_CODE_END
  .global rvtest_code_end
rvtest_code_end:
.endm
