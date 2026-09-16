#pragma once

#include "qsbit/error.hpp"
#include <cstdint>

namespace qsbit::rv32 {
enum class Op {
  Lui,
  Auipc,
  Jal,
  Jalr,
  Beq,
  Bne,
  Blt,
  Bge,
  Bltu,
  Bgeu,
  Lb,
  Lh,
  Lw,
  Lbu,
  Lhu,
  Sb,
  Sh,
  Sw,
  Addi,
  Slti,
  Sltiu,
  Xori,
  Ori,
  Andi,
  Slli,
  Srli,
  Srai,
  Add,
  Sub,
  Sll,
  Slt,
  Sltu,
  Xor,
  Srl,
  Sra,
  Or,
  And,
  Fence,
  Ecall,
  Ebreak,
  Quantum
};
struct Decoded {
  Op op;
  std::uint32_t word;
  std::uint32_t immediate = 0;
  std::uint8_t rd = 0, rs1 = 0, rs2 = 0;
  bool reads_rs1 = false, reads_rs2 = false, writes_rd = false;
};
enum class MemoryKind { None, Load, Store };
struct Effect {
  std::uint32_t next_pc = 0, value = 0, address = 0, store_value = 0;
  bool writes_rd = false, branch_taken = false, sign_extend_load = false;
  MemoryKind memory = MemoryKind::None;
  std::uint8_t width = 0;
};
[[nodiscard]] Decoded decode(std::uint32_t word);
[[nodiscard]] Effect evaluate(const Decoded &instruction, std::uint32_t pc, std::uint32_t lhs,
                              std::uint32_t rhs);
[[nodiscard]] std::uint32_t sign_extend(std::uint32_t value, unsigned bits);
} // namespace qsbit::rv32
