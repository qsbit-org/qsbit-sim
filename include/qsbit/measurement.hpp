#pragma once
#include "qsbit/time.hpp"
namespace qsbit {
enum class ExecutionFlag { Always, LastOne, LastZero, Equal };
struct MeasurementReference {
  Epoch epoch = 0;
  Id measurement = 0;
  std::uint32_t target = 0;
  bool operator==(const MeasurementReference &) const = default;
};
struct Completion {
  MeasurementReference reference;
  bool value = false;
};
} // namespace qsbit
