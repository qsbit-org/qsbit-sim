#pragma once
#include <filesystem>
#include <string>

namespace qsbit {
inline constexpr unsigned PythonBackendApiVersion = 4;
struct RuntimeLayout {
  bool installed = false;
  bool python_enabled = false;
  std::filesystem::path web_root, python, backend;
  std::string version, revision;
};
RuntimeLayout runtime_layout();
std::string installation_info(const RuntimeLayout &layout);
} // namespace qsbit
