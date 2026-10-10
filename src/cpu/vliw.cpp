#include "qsbit/cpu/vliw.hpp"
#include <utility>

namespace qsbit {
std::array<ControlOperation, 2> decode_cw_bundle(std::uint32_t word, Id instruction,
                                                 const std::array<std::uint32_t, 32> &registers) {
  require((word & 0x8000007fU) == 0x2bU, ErrorCode::IllegalInstruction,
          "invalid cw bundle encoding");
  std::array<ControlOperation, 2> operations;
  for (std::size_t lane = 0; lane < operations.size(); ++lane) {
    const auto slot = (word >> (7 + 12 * lane)) & 0xfffU;
    const auto port = slot & 31U;
    const auto codeword = (slot >> 5) & 31U;
    const auto modes = slot >> 10;
    const auto scalar = 0x0bU | (port << 15) | (codeword << 20) | (modes << 25);
    operations[lane] =
        adapt_quantum(rv32::decode(scalar), instruction, registers[port], registers[codeword]);
  }
  return operations;
}
} // namespace qsbit
