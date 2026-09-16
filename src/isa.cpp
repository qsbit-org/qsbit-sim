#include "qsbit/isa.hpp"
#include <bit>

namespace qsbit::rv32 {
std::uint32_t sign_extend(std::uint32_t value, unsigned bits) {
  require(bits > 0 && bits <= 32, ErrorCode::InvalidOperand, "invalid sign extension width");
  if (bits == 32)
    return value;
  const auto mask = (std::uint32_t{1} << bits) - 1;
  const auto sign = std::uint32_t{1} << (bits - 1);
  return ((value & mask) ^ sign) - sign;
}
Decoded decode(std::uint32_t w) {
  Decoded d{Op::Fence, w};
  d.rd = static_cast<std::uint8_t>((w >> 7) & 31);
  d.rs1 = static_cast<std::uint8_t>((w >> 15) & 31);
  d.rs2 = static_cast<std::uint8_t>((w >> 20) & 31);
  const auto f3 = (w >> 12) & 7;
  const auto f7 = w >> 25;
  const auto illegal = [&] {
    throw Fault(ErrorCode::IllegalInstruction, "unsupported RV32 encoding");
  };
  switch (w & 127) {
  case 0x37:
    d.op = Op::Lui;
    d.immediate = w & 0xfffff000U;
    d.writes_rd = true;
    break;
  case 0x17:
    d.op = Op::Auipc;
    d.immediate = w & 0xfffff000U;
    d.writes_rd = true;
    break;
  case 0x6f:
    d.op = Op::Jal;
    d.writes_rd = true;
    d.immediate = sign_extend(((w >> 31) << 20) | (w & 0xff000U) | (((w >> 20) & 1) << 11) |
                                  (((w >> 21) & 1023) << 1),
                              21);
    break;
  case 0x67:
    if (f3 != 0)
      illegal();
    d.op = Op::Jalr;
    d.writes_rd = true;
    d.reads_rs1 = true;
    d.immediate = sign_extend(w >> 20, 12);
    break;
  case 0x63:
    switch (f3) {
    case 0:
      d.op = Op::Beq;
      break;
    case 1:
      d.op = Op::Bne;
      break;
    case 4:
      d.op = Op::Blt;
      break;
    case 5:
      d.op = Op::Bge;
      break;
    case 6:
      d.op = Op::Bltu;
      break;
    case 7:
      d.op = Op::Bgeu;
      break;
    default:
      illegal();
    }
    d.reads_rs1 = d.reads_rs2 = true;
    d.immediate = sign_extend(((w >> 31) << 12) | (((w >> 7) & 1) << 11) | (((w >> 25) & 63) << 5) |
                                  (((w >> 8) & 15) << 1),
                              13);
    break;
  case 0x03:
    switch (f3) {
    case 0:
      d.op = Op::Lb;
      break;
    case 1:
      d.op = Op::Lh;
      break;
    case 2:
      d.op = Op::Lw;
      break;
    case 4:
      d.op = Op::Lbu;
      break;
    case 5:
      d.op = Op::Lhu;
      break;
    default:
      illegal();
    }
    d.reads_rs1 = true;
    d.writes_rd = true;
    d.immediate = sign_extend(w >> 20, 12);
    break;
  case 0x23:
    switch (f3) {
    case 0:
      d.op = Op::Sb;
      break;
    case 1:
      d.op = Op::Sh;
      break;
    case 2:
      d.op = Op::Sw;
      break;
    default:
      illegal();
    }
    d.reads_rs1 = d.reads_rs2 = true;
    d.immediate = sign_extend(((w >> 25) << 5) | ((w >> 7) & 31), 12);
    break;
  case 0x13:
    d.reads_rs1 = true;
    d.writes_rd = true;
    d.immediate = sign_extend(w >> 20, 12);
    switch (f3) {
    case 0:
      d.op = Op::Addi;
      break;
    case 2:
      d.op = Op::Slti;
      break;
    case 3:
      d.op = Op::Sltiu;
      break;
    case 4:
      d.op = Op::Xori;
      break;
    case 6:
      d.op = Op::Ori;
      break;
    case 7:
      d.op = Op::Andi;
      break;
    case 1:
      if (f7 != 0)
        illegal();
      d.op = Op::Slli;
      d.immediate = d.rs2;
      break;
    case 5:
      if (f7 != 0 && f7 != 32)
        illegal();
      d.op = f7 == 0 ? Op::Srli : Op::Srai;
      d.immediate = d.rs2;
      break;
    default:
      illegal();
    }
    break;
  case 0x33:
    d.reads_rs1 = d.reads_rs2 = d.writes_rd = true;
    if (f7 != 0 && !((f3 == 0 || f3 == 5) && f7 == 32))
      illegal();
    switch (f3) {
    case 0:
      d.op = f7 == 0 ? Op::Add : Op::Sub;
      break;
    case 1:
      d.op = Op::Sll;
      break;
    case 2:
      d.op = Op::Slt;
      break;
    case 3:
      d.op = Op::Sltu;
      break;
    case 4:
      d.op = Op::Xor;
      break;
    case 5:
      d.op = f7 == 0 ? Op::Srl : Op::Sra;
      break;
    case 6:
      d.op = Op::Or;
      break;
    case 7:
      d.op = Op::And;
      break;
    }
    break;
  case 0x0f:
    if (f3 != 0)
      illegal();
    d.op = Op::Fence;
    break;
  case 0x73:
    if (w == 0x00000073)
      d.op = Op::Ecall;
    else if (w == 0x00100073)
      d.op = Op::Ebreak;
    else
      illegal();
    break;
  case 0x0b:
    if (f3 == 7 || (f3 == 5 ? f7 > 1 : f7 != 0))
      illegal();
    if ((f3 == 1 && (d.rd != 0 || d.rs2 != 0)) ||
        ((f3 == 2 || f3 == 4 || f3 == 6) && (d.rd != 0 || d.rs1 != 0 || d.rs2 != 0)) ||
        (f3 == 3 && d.rs2 != 0))
      illegal();
    d.op = Op::Quantum;
    d.reads_rs1 = f3 == 0 || f3 == 1 || f3 == 3 || f3 == 5;
    d.reads_rs2 = f3 == 0 || f3 == 5;
    d.writes_rd = f3 == 0 || f3 == 3;
    break;
  default:
    illegal();
  }
  return d;
}
Effect evaluate(const Decoded &d, std::uint32_t pc, std::uint32_t a, std::uint32_t b) {
  require((pc & 3) == 0, ErrorCode::InstructionMisaligned, "unaligned instruction PC");
  const auto signed_value = [](std::uint32_t x) { return std::bit_cast<std::int32_t>(x); };
  const auto sar = [&](std::uint32_t x, std::uint32_t amount) {
    return std::bit_cast<std::uint32_t>(signed_value(x) >> (amount & 31));
  };
  Effect e;
  e.next_pc = pc + 4;
  e.writes_rd = d.writes_rd;
  const auto i = d.immediate;
  switch (d.op) {
  case Op::Lui:
    e.value = i;
    break;
  case Op::Auipc:
    e.value = pc + i;
    break;
  case Op::Jal:
    e.value = pc + 4;
    e.next_pc = pc + i;
    e.branch_taken = true;
    break;
  case Op::Jalr:
    e.value = pc + 4;
    e.next_pc = (a + i) & ~1U;
    e.branch_taken = true;
    break;
  case Op::Beq:
    e.branch_taken = a == b;
    break;
  case Op::Bne:
    e.branch_taken = a != b;
    break;
  case Op::Blt:
    e.branch_taken = signed_value(a) < signed_value(b);
    break;
  case Op::Bge:
    e.branch_taken = signed_value(a) >= signed_value(b);
    break;
  case Op::Bltu:
    e.branch_taken = a < b;
    break;
  case Op::Bgeu:
    e.branch_taken = a >= b;
    break;
  case Op::Lb:
  case Op::Lh:
  case Op::Lw:
  case Op::Lbu:
  case Op::Lhu:
    e.memory = MemoryKind::Load;
    e.address = a + i;
    e.width = (d.op == Op::Lb || d.op == Op::Lbu) ? 1 : (d.op == Op::Lw ? 4 : 2);
    e.sign_extend_load = d.op == Op::Lb || d.op == Op::Lh;
    break;
  case Op::Sb:
  case Op::Sh:
  case Op::Sw:
    e.memory = MemoryKind::Store;
    e.address = a + i;
    e.store_value = b;
    e.width = d.op == Op::Sb ? 1 : (d.op == Op::Sh ? 2 : 4);
    break;
  case Op::Addi:
    e.value = a + i;
    break;
  case Op::Slti:
    e.value = signed_value(a) < signed_value(i);
    break;
  case Op::Sltiu:
    e.value = a < i;
    break;
  case Op::Xori:
    e.value = a ^ i;
    break;
  case Op::Ori:
    e.value = a | i;
    break;
  case Op::Andi:
    e.value = a & i;
    break;
  case Op::Slli:
    e.value = a << (i & 31);
    break;
  case Op::Srli:
    e.value = a >> (i & 31);
    break;
  case Op::Srai:
    e.value = sar(a, i);
    break;
  case Op::Add:
    e.value = a + b;
    break;
  case Op::Sub:
    e.value = a - b;
    break;
  case Op::Sll:
    e.value = a << (b & 31);
    break;
  case Op::Slt:
    e.value = signed_value(a) < signed_value(b);
    break;
  case Op::Sltu:
    e.value = a < b;
    break;
  case Op::Xor:
    e.value = a ^ b;
    break;
  case Op::Srl:
    e.value = a >> (b & 31);
    break;
  case Op::Sra:
    e.value = sar(a, b);
    break;
  case Op::Or:
    e.value = a | b;
    break;
  case Op::And:
    e.value = a & b;
    break;
  case Op::Fence:
    break;
  case Op::Ecall:
    throw Fault(ErrorCode::EnvironmentCall, "RV32I ECALL");
  case Op::Ebreak:
    throw Fault(ErrorCode::Breakpoint, "RV32I EBREAK");
  case Op::Quantum:
    break;
  }
  if (d.op >= Op::Beq && d.op <= Op::Bgeu && e.branch_taken)
    e.next_pc = pc + i;
  require((e.next_pc & 3) == 0, ErrorCode::InstructionMisaligned, "unaligned taken branch target");
  return e;
}
} // namespace qsbit::rv32
