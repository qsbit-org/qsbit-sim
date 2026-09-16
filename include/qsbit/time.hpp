#pragma once

#include "qsbit/error.hpp"
#include <cstdint>
#include <limits>

namespace qsbit {
using Tick = std::uint64_t;
using Id = std::uint64_t;
using Epoch = std::uint64_t;

inline Tick checked_add(Tick a, Tick b) {
  require(b <= std::numeric_limits<Tick>::max() - a, ErrorCode::TimeOverflow,
          "tick addition overflow");
  return a + b;
}
inline Tick checked_mul(Tick a, Tick b) {
  require(a == 0 || b <= std::numeric_limits<Tick>::max() / a, ErrorCode::TimeOverflow,
          "tick multiplication overflow");
  return a * b;
}
struct Clock {
  Tick period = 1;
  Tick phase = 0;
  void validate() const {
    require(period > 0 && phase < period, ErrorCode::InvalidProfile,
            "invalid clock period or phase");
  }
  [[nodiscard]] bool edge(Tick t) const { return t >= phase && (t - phase) % period == 0; }
  [[nodiscard]] Tick after(Tick publication, std::uint32_t latency = 1) const {
    validate();
    require(latency > 0, ErrorCode::InvalidProfile, "crossing latency must be positive");
    Tick first = phase;
    if (publication >= phase)
      first =
          checked_add(phase, checked_mul(checked_add((publication - phase) / period, 1), period));
    return checked_add(first, checked_mul(latency - 1, period));
  }
};
} // namespace qsbit
