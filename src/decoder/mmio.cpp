#include "qsbit/decoder/mmio.hpp"
#include "qsbit/contracts/decoder.hpp"
#include "qsbit/error.hpp"
#include "qsbit/image.hpp"
#include "qsbit/memory.hpp"
#include <cstdint>
#include <optional>
#include <set>
#include <utility>

namespace qsbit {
namespace registers = contract::decoder;
DecoderMmio::DecoderMmio(std::uint32_t base, std::set<std::uint32_t> decoders)
    : base_(base), decoders_(std::move(decoders)) {
  require(base % 4 == 0 && base <= UINT32_MAX - (registers::RegisterBytes - 1),
          ErrorCode::InvalidProfile, "invalid decoder MMIO address");
}
bool DecoderMmio::contains(std::uint32_t address) const {
  return !decoders_.empty() && address >= base_ &&
         std::uint64_t(address) < std::uint64_t(base_) + registers::RegisterBytes;
}
void DecoderMmio::validate_memory(const ProgramImage &image) const {
  require(decoders_.empty() || std::uint64_t(image.base()) + image.bytes().size() <= base_ ||
              std::uint64_t(base_) + registers::RegisterBytes <= image.base(),
          ErrorCode::InvalidProfile, "decoder MMIO overlaps program RAM");
}
std::optional<DecoderCommand> DecoderMmio::access(std::uint32_t core,
                                                  const MemoryRequest &request) {
  require(contains(request.address) && request.width == 4 && request.address % 4 == 0 &&
              !request.instruction,
          ErrorCode::InvalidOperand, "decoder MMIO requires aligned data words");
  auto &r = registers_[core];
  const auto offset = request.address - base_;
  if (request.write) {
    switch (offset) {
    case registers::Select:
      require(decoders_.contains(request.value), ErrorCode::InvalidOperand, "unknown decoder id");
      r.decoder = request.value;
      return {};
    case registers::Count:
      r.count = request.value;
      return {};
    case registers::DataLow:
      r.low = request.value;
      return {};
    case registers::DataHigh:
      r.high = request.value;
      return {};
    case registers::Tag:
      r.tag = request.value;
      return {};
    case registers::Command:
      require(decoders_.contains(r.decoder), ErrorCode::InvalidOperand, "unknown decoder id");
      switch (request.value) {
      case registers::Consume:
        return DecoderConsume{r.decoder};
      case registers::Reset:
        return DecoderReset{r.decoder, r.tag};
      case registers::Submit:
        require(r.count > 0 && r.count <= 64, ErrorCode::InvalidOperand,
                "decoder packet must contain 1 to 64 bits");
        return DecoderSubmit{r.decoder, r.count, r.tag,
                             std::uint64_t(r.low) | (std::uint64_t(r.high) << 32)};
      default:
        throw Fault(ErrorCode::InvalidOperand, "invalid decoder command");
      }
    default:
      throw Fault(ErrorCode::InvalidOperand, "read-only decoder register");
    }
  }
  require(decoders_.contains(r.decoder), ErrorCode::InvalidOperand, "unknown decoder id");
  switch (offset) {
  case registers::Count:
    return DecoderRead{r.decoder, DecoderReadKind::Count};
  case registers::Command:
    return DecoderRead{r.decoder, DecoderReadKind::Status};
  case registers::ResultLow:
    return DecoderRead{r.decoder, DecoderReadKind::Low};
  case registers::ResultHigh:
    return DecoderRead{r.decoder, DecoderReadKind::High};
  default:
    throw Fault(ErrorCode::Protocol, "decoder result read before completion");
  }
}
} // namespace qsbit
