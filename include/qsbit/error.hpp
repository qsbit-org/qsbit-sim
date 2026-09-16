#pragma once

#include <stdexcept>
#include <string>

namespace qsbit {

enum class ErrorCode {
  InvalidProfile,
  TimeOverflow,
  IllegalInstruction,
  InstructionMisaligned,
  InstructionAccess,
  LoadMisaligned,
  StoreMisaligned,
  LoadAccess,
  StoreAccess,
  EnvironmentCall,
  Breakpoint,
  InvalidImage,
  UnsupportedSynchronization,
  InvalidPort,
  InvalidCodeword,
  InvalidOperand,
  Capacity,
  LateAdmission,
  ManifestMismatch,
  Protocol,
  ResourceConflict,
  UnsupportedCapability,
  InvalidToken,
  DuplicateResult,
  BackendFailure,
  Watchdog
};

class Fault final : public std::runtime_error {
public:
  Fault(ErrorCode code, const std::string &message) : std::runtime_error(message), code_(code) {}
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
  ErrorCode code_;
};

[[nodiscard]] const char *name(ErrorCode code) noexcept;
void require(bool condition, ErrorCode code, const std::string &message);

} // namespace qsbit
