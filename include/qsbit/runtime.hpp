#pragma once
#include <filesystem>
#include <string>

namespace qsbit {
struct RuntimeLayout {
  bool installed = false;
  bool python_enabled = false;
  std::filesystem::path web_root, python, backend;
  std::string version, revision;
};
RuntimeLayout runtime_layout();
std::string installation_info();
} // namespace qsbit
