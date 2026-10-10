#pragma once
#include "qsbit/time.hpp"
#include <variant>
namespace qsbit {
struct CodewordCommand {
  std::uint32_t port, codeword;
  bool operator==(const CodewordCommand &) const = default;
};
struct WaitCommand {
  std::uint32_t cycles;
  bool operator==(const WaitCommand &) const = default;
};
struct FetchMeasurementCommand {
  std::uint32_t qubit;
  bool operator==(const FetchMeasurementCommand &) const = default;
};
struct HaltCommand {
  bool operator==(const HaltCommand &) const = default;
};
struct SynchronizeCommand {
  std::uint32_t target;
  bool operator==(const SynchronizeCommand &) const = default;
};
struct ControlOperation {
  Id instruction = 0;
  std::variant<WaitCommand, CodewordCommand, FetchMeasurementCommand, HaltCommand,
               SynchronizeCommand>
      command = WaitCommand{0};
  template <typename T> const T &get() const { return std::get<T>(command); }
  template <typename T> bool is() const { return std::holds_alternative<T>(command); }
  bool operator==(const ControlOperation &) const = default;
};
namespace rv32 {
struct Decoded;
}
[[nodiscard]] ControlOperation adapt_quantum(const rv32::Decoded &, Id instruction,
                                             std::uint32_t lhs, std::uint32_t rhs);
} // namespace qsbit
