#pragma once

#include "qsbit/image.hpp"
#include "qsbit/mailbox.hpp"
#include <array>
#include <optional>

namespace qsbit {
struct MemoryRequest {
  Id id = 0;
  std::uint32_t address = 0, value = 0;
  std::uint8_t width = 4;
  bool write = false, instruction = false;
};
struct MemoryResponse {
  Id id = 0;
  std::uint32_t value = 0;
  std::optional<ErrorCode> fault;
};
struct MemoryPort {
  Mailbox<MemoryRequest> requests;
  Mailbox<MemoryResponse> responses;
  explicit MemoryPort(Clock clock) : requests(1, clock), responses(1, clock) {}
  void reset() {
    requests.reset();
    responses.reset();
  }
};
class MemoryModel {
public:
  MemoryModel(ProgramImage image, Clock clock, std::uint32_t latency);
  void step(Tick now, Epoch epoch, MemoryPort &fetch, MemoryPort &data);
  void reset();
  [[nodiscard]] const ProgramImage &image() const { return image_; }
  [[nodiscard]] bool idle() const { return !pending_[0] && !pending_[1]; }

private:
  struct Pending {
    MemoryRequest request;
    Tick completion;
    Epoch epoch;
  };
  void step_port(Tick now, Epoch epoch, MemoryPort &port, std::size_t index);
  ProgramImage image_;
  Clock clock_;
  std::uint32_t latency_;
  std::array<std::optional<Pending>, 2> pending_;
  std::array<Id, 2> last_id_{};
};
} // namespace qsbit
