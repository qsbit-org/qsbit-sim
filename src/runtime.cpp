#include "qsbit/runtime.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <stdexcept>
#include <string>
#ifdef __APPLE__
#include <cstdint>
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace qsbit {
namespace {
std::filesystem::path executable_path() {
#if defined(__linux__)
  return std::filesystem::canonical("/proc/self/exe");
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  // dyld reports the required buffer size, including the terminating null.
  (void)_NSGetExecutablePath(nullptr, &size);
  if (size == 0)
    throw std::runtime_error("Cannot determine the simulator executable path.");
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    throw std::runtime_error("Cannot determine the simulator executable path.");
  return std::filesystem::canonical(buffer.data());
#else
#error "Executable path discovery requires a supported platform implementation."
#endif
}
} // namespace

RuntimeLayout runtime_layout() {
  RuntimeLayout layout;
  const auto executable = executable_path();
  layout.installed = executable.filename() == "qsbit-sim-native";
  if (layout.installed) {
    const auto root = executable.parent_path();
    std::ifstream input(root / "runtime.json");
    if (!input)
      throw std::runtime_error("Missing simulator runtime.json; reinstall qsbit-sim.");
    const auto metadata = nlohmann::json::parse(input);
    if (metadata.at("schema") != 1)
      throw std::runtime_error("Unsupported simulator installation schema.");
    layout.version = metadata.at("version");
    layout.revision = metadata.at("revision");
    layout.python_enabled = metadata.at("python_enabled");
    layout.web_root = root / "web";
  } else {
    layout.version = QSBIT_RUNTIME_VERSION;
    layout.revision = QSBIT_RUNTIME_REVISION;
    layout.web_root = QSBIT_BUILD_WEB;
    layout.python_enabled = QSBIT_RUNTIME_PYTHON;
  }
  if (layout.python_enabled) {
    if (const auto *selected = std::getenv("QSBIT_PYTHON"); selected && *selected)
      layout.python = std::filesystem::absolute(selected);
    else if (const auto *environment = std::getenv("VIRTUAL_ENV"); environment && *environment)
      layout.python = std::filesystem::absolute(std::filesystem::path(environment) / "bin/python");
    else if (!layout.installed)
      layout.python = QSBIT_BUILD_PYTHON;
    else if (const auto *search = std::getenv("PATH")) {
      const std::string paths(search);
      std::size_t start = 0;
      do {
        const auto end = paths.find(':', start);
        const auto directory = paths.substr(start, end - start);
        const auto candidate =
            std::filesystem::path(directory.empty() ? "." : directory) / QSBIT_PYTHON_COMMAND;
        if (std::filesystem::is_regular_file(candidate)) {
          layout.python = std::filesystem::absolute(candidate);
          break;
        }
        if (end == std::string::npos)
          break;
        start = end + 1;
      } while (start <= paths.size());
    }
    if (!std::filesystem::is_regular_file(layout.python))
      throw std::runtime_error("Python interpreter is missing. Activate a compatible venv with "
                               "qsbit-sim-backends installed, or set QSBIT_PYTHON.");
  }
  return layout;
}
std::string installation_info(const RuntimeLayout &layout) {
  nlohmann::json info{{"schema", 1},
                      {"version", layout.version},
                      {"revision", layout.revision},
                      {"installed", layout.installed},
                      {"player_api", 1},
                      {"web_root", layout.web_root.string()},
                      {"python_enabled", layout.python_enabled},
                      {"python", nullptr},
                      {"backend", nullptr}};
  if (layout.python_enabled) {
    info["python"] = layout.python.string();
    info["backend"] = layout.backend.string();
  }
  return info.dump();
}
} // namespace qsbit
