#pragma once
#include <cstdint>
#include <string>
namespace qsbit::contract {
inline constexpr std::uint32_t ControlOpcode = 0x0b;
inline constexpr std::uint32_t BundleOpcode = 0x2b;
enum class ControlFunction : std::uint32_t {
  Codeword = 0,
  Wait = 1,
  WaitImmediate = 2,
  FetchMeasurement = 3,
  Synchronize = 6
};
constexpr bool valid_control_word(std::uint32_t word) {
  if ((word & 127U) != ControlOpcode)
    return false;
  const auto rd = (word >> 7) & 31U;
  const auto rs2 = (word >> 20) & 31U;
  const auto mode = word >> 25;
  switch (static_cast<ControlFunction>((word >> 12) & 7U)) {
  case ControlFunction::Codeword:
    return mode <= 3 && rd == 0;
  case ControlFunction::Wait:
    return mode == 0 && rd == 0 && rs2 == 0;
  case ControlFunction::WaitImmediate:
  case ControlFunction::Synchronize:
    return rd == 0;
  case ControlFunction::FetchMeasurement:
    return mode == 0 && rs2 == 0;
  }
  return false;
}
inline std::string instruction(ControlFunction function, const std::string &operands) {
  return ".insn r " + std::to_string(ControlOpcode) + ", " +
         std::to_string(static_cast<std::uint32_t>(function)) + ", 0, " + operands;
}
} // namespace qsbit::contract
