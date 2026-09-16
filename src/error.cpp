#include "qsbit/error.hpp"

namespace qsbit {
const char *name(ErrorCode code) noexcept {
#define QS_CASE(value)                                                                             \
  case ErrorCode::value:                                                                           \
    return #value
  switch (code) {
    QS_CASE(InvalidProfile);
    QS_CASE(TimeOverflow);
    QS_CASE(IllegalInstruction);
    QS_CASE(InstructionMisaligned);
    QS_CASE(InstructionAccess);
    QS_CASE(LoadMisaligned);
    QS_CASE(StoreMisaligned);
    QS_CASE(LoadAccess);
    QS_CASE(StoreAccess);
    QS_CASE(EnvironmentCall);
    QS_CASE(Breakpoint);
    QS_CASE(InvalidImage);
    QS_CASE(UnsupportedSynchronization);
    QS_CASE(InvalidPort);
    QS_CASE(InvalidCodeword);
    QS_CASE(InvalidOperand);
    QS_CASE(Capacity);
    QS_CASE(LateAdmission);
    QS_CASE(ManifestMismatch);
    QS_CASE(Protocol);
    QS_CASE(ResourceConflict);
    QS_CASE(UnsupportedCapability);
    QS_CASE(InvalidToken);
    QS_CASE(DuplicateResult);
    QS_CASE(BackendFailure);
    QS_CASE(Watchdog);
  }
#undef QS_CASE
  return "UnknownError";
}
void require(bool condition, ErrorCode code, const std::string &message) {
  if (!condition)
    throw Fault(code, message);
}
} // namespace qsbit
