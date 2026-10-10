#pragma once

#include "qsbit/memory.hpp"
#include <map>
#include <optional>
#include <set>
#include <variant>

namespace qsbit {
struct DecoderSubmit {
  std::uint32_t decoder, count, tag;
  std::uint64_t data;
};
struct DecoderReset {
  std::uint32_t decoder, tag;
};
struct DecoderConsume {
  std::uint32_t decoder;
};
enum class DecoderReadKind { Count, Status, Low, High };
struct DecoderRead {
  std::uint32_t decoder;
  DecoderReadKind kind;
};
using DecoderCommand = std::variant<DecoderSubmit, DecoderReset, DecoderConsume, DecoderRead>;
class DecoderMmio {
public:
  DecoderMmio(std::uint32_t base, std::set<std::uint32_t> decoders);
  [[nodiscard]] bool contains(std::uint32_t address) const;
  void validate_memory(const ProgramImage &image) const;
  [[nodiscard]] std::optional<DecoderCommand> access(std::uint32_t core,
                                                     const MemoryRequest &request);
  void reset() { registers_.clear(); }

private:
  struct Registers {
    std::uint32_t decoder = 0, count = 0, low = 0, high = 0, tag = 0;
  };
  std::uint32_t base_;
  std::set<std::uint32_t> decoders_;
  std::map<std::uint32_t, Registers> registers_;
};
} // namespace qsbit
