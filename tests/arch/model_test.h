// Bare-metal, unprivileged RV32I platform hooks for the pinned architecture suite.
// Signatures and every retired state are checked by an independent reference runner.
#define RVMODEL_BOOT .global _start; _start:
#define RVMODEL_HALT .global qsbit_test_halt; qsbit_test_halt: .word 0x0000400b
#define RVMODEL_DATA_BEGIN .align 4; .global begin_signature; begin_signature:
#define RVMODEL_DATA_END .global end_signature; end_signature:
#define RVMODEL_IO_INIT
#define RVMODEL_IO_WRITE_STR(_R, _STR)
#define RVMODEL_IO_CHECK()
#define RVMODEL_IO_ASSERT_GPR_EQ(_SCRATCH, _VALUE, _EXPECTED)
#define RVMODEL_IO_ASSERT_SFPR_EQ(_SCRATCH, _VALUE, _EXPECTED)
#define RVMODEL_IO_ASSERT_DFPR_EQ(_SCRATCH, _VALUE, _EXPECTED)
