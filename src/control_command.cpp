#include "qsbit/control_command.hpp"
#include "qsbit/isa.hpp"

namespace qsbit {
ControlOperation adapt_quantum(const rv32::Decoded &d, Id id, std::uint32_t lhs,
                               std::uint32_t rhs) {
  require(d.op == rv32::Op::Quantum, ErrorCode::Protocol, "non-quantum operation sent to adapter");
  switch ((d.word >> 12) & 7) {
  case 0:
    return {id,
            CodewordCommand{(d.word >> 25) & 1 ? d.rs1 : lhs, (d.word >> 25) & 2 ? d.rs2 : rhs}};
  case 1:
    return {id, WaitCommand{lhs}};
  case 2:
    return {id, WaitCommand{d.word >> 15}};
  case 3:
    return {id, FetchMeasurementCommand{d.rs1}};
  case 6:
    return {id, SynchronizeCommand{d.word >> 15}};
  default:
    throw Fault(ErrorCode::IllegalInstruction, "reserved quantum operation");
  }
}
} // namespace qsbit
