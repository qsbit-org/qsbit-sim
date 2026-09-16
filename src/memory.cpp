#include "qsbit/memory.hpp"
#include <utility>

namespace qsbit {
MemoryModel::MemoryModel(ProgramImage image, Clock clock, std::uint32_t latency)
    : image_(std::move(image)), clock_(clock), latency_(latency) {
  clock_.validate();
  require(latency > 0, ErrorCode::InvalidProfile, "memory latency must be positive");
}
void MemoryModel::reset() {
  pending_ = {};
  last_id_ = {};
}
void MemoryModel::step(Tick now, Epoch epoch, MemoryPort &fetch, MemoryPort &data) {
  require(clock_.edge(now), ErrorCode::Protocol, "memory invoked off-edge");
  step_port(now, epoch, fetch, 0);
  step_port(now, epoch, data, 1);
}
void MemoryModel::step_port(Tick now, Epoch epoch, MemoryPort &port, std::size_t index) {
  auto &pending = pending_[index];
  const bool was_busy = pending.has_value();
  if (pending && pending->completion <= now && !port.responses.full()) {
    if (pending->epoch == epoch) {
      const auto &request = pending->request;
      MemoryResponse response;
      response.id = request.id;
      try {
        if (request.write)
          image_.write(request.address, request.width, request.value);
        else
          response.value = image_.read(request.address, request.width, request.instruction);
      } catch (const Fault &fault) {
        response.fault = fault.code();
      }
      port.responses.publish(now, epoch, response);
    }
    pending.reset();
  }
  if (!was_busy && !port.responses.full()) {
    if (auto message = port.requests.take(now)) {
      if (message->epoch != epoch)
        return;
      const auto &request = message->value;
      require(request.id > last_id_[index] && request.id != 0, ErrorCode::Protocol,
              "memory request identity reused or reordered");
      require(request.instruction == (index == 0) && !(request.instruction && request.write),
              ErrorCode::Protocol, "wrong memory request port");
      last_id_[index] = request.id;
      pending = Pending{request, checked_add(now, checked_mul(latency_, clock_.period)), epoch};
    }
  }
}
} // namespace qsbit
