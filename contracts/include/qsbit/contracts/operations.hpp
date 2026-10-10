#pragma once
#include <array>
#include <optional>
#include <string_view>
#include <utility>
namespace qsbit::contract {
enum class QuantumOp { H, X, Z, S, Sdg, T, Tdg, Cx, MeasureZ, Reset };
inline constexpr std::array operation_names{
    std::pair{QuantumOp::H, std::string_view{"h"}},
    std::pair{QuantumOp::X, std::string_view{"x"}},
    std::pair{QuantumOp::Z, std::string_view{"z"}},
    std::pair{QuantumOp::S, std::string_view{"s"}},
    std::pair{QuantumOp::Sdg, std::string_view{"sdg"}},
    std::pair{QuantumOp::T, std::string_view{"t"}},
    std::pair{QuantumOp::Tdg, std::string_view{"tdg"}},
    std::pair{QuantumOp::Cx, std::string_view{"cx"}},
    std::pair{QuantumOp::MeasureZ, std::string_view{"measure"}},
    std::pair{QuantumOp::Reset, std::string_view{"reset"}}};
constexpr std::string_view operation_name(QuantumOp op) {
  for (auto [value, name] : operation_names)
    if (value == op)
      return name;
  return {};
}
constexpr std::optional<QuantumOp> parse_operation(std::string_view name) {
  for (auto [value, candidate] : operation_names)
    if (candidate == name)
      return value;
  return std::nullopt;
}
} // namespace qsbit::contract
