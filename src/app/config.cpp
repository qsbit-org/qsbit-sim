#include "config.hpp"
#include "qsbit/cpu/vliw.hpp"
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

namespace qsbit::app {
std::uint64_t number(const std::string &text) {
  require(!text.empty() && text.front() != '-', ErrorCode::InvalidOperand,
          "expected a nonnegative integer");
  std::size_t end = 0;
  const auto value = std::stoull(text, &end, 0);
  require(end == text.size(), ErrorCode::InvalidOperand, "invalid integer argument");
  return value;
}
std::uint32_t word(const std::string &text) {
  const auto value = number(text);
  require(value <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::InvalidOperand,
          "argument exceeds uint32");
  return static_cast<std::uint32_t>(value);
}
std::uint64_t json_number(const Json &value, const std::string &key) {
  require(value.is_number_integer() &&
              (value.is_number_unsigned() || value.get<std::int64_t>() >= 0),
          ErrorCode::InvalidProfile, key + " must be a nonnegative integer");
  return value.get<std::uint64_t>();
}
std::uint32_t json_word(const Json &value, const std::string &key) {
  const auto result = json_number(value, key);
  require(result <= std::numeric_limits<std::uint32_t>::max(), ErrorCode::InvalidProfile,
          key + " exceeds uint32");
  return static_cast<std::uint32_t>(result);
}
std::string json_string(const Json &value, const std::string &key) {
  require(value.is_string(), ErrorCode::InvalidProfile, key + " must be a string");
  return value.get<std::string>();
}
Json read_json(const std::filesystem::path &path) {
  std::ifstream stream(path);
  require(bool(stream), ErrorCode::InvalidProfile, "cannot read " + path.string());
  Json value;
  stream >> value;
  return value;
}
std::string relative_to(const std::filesystem::path &base, const Json &value,
                        const std::string &key) {
  return (base / json_string(value, key)).lexically_normal().string();
}
void ensure_parent(const std::string &path) {
  const auto parent = std::filesystem::path(path).parent_path();
  if (!parent.empty())
    std::filesystem::create_directories(parent);
}
void write_json(const std::string &path, const Json &value) {
  ensure_parent(path);
  std::ofstream stream(path);
  require(bool(stream), ErrorCode::InvalidOperand, "cannot open " + path);
  stream << value.dump(2) << '\n';
  require(bool(stream), ErrorCode::Protocol, "cannot write " + path);
}

namespace {
Core::CpuFactory cpu_factory(const std::string &model) {
  require(model == "rv32" || model == "vliw", ErrorCode::InvalidProfile,
          "cpu_model must be rv32 or vliw");
  if (model == "vliw")
    return [](Clock clock, std::uint32_t entry, Trace &trace) {
      return std::make_unique<VliwCpuCycleModel>(clock, entry, trace);
    };
  return {};
}

} // namespace
CoreSetup configure_cores(const Profile &profile, const std::string &program,
                          const Json &core_settings, const std::string &cpu_model,
                          const std::filesystem::path &config_base, MemoryConfig memory) {
  const auto load_image = [&](const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), ErrorCode::InvalidImage, "cannot read program: " + path);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    return memory.raw_base ? ProgramImage::raw(bytes, *memory.raw_base, memory.base, memory.size)
                           : ProgramImage::elf(bytes, memory.base, memory.size);
  };
  std::vector<CoreConfig> cores;
  std::vector<std::string> models;
  if (core_settings.empty()) {
    cores.push_back({0, profile, load_image(program), cpu_factory(cpu_model)});
    models.push_back(cpu_model);
  } else {
    const std::set<std::string> allowed{"id",           "program",   "profile",
                                        "profile_file", "cpu_model", "sync_capacity"};
    for (const auto &settings : core_settings) {
      require(settings.is_object() && settings.contains("id") && settings.contains("program"),
              ErrorCode::InvalidProfile, "each core requires id and program");
      for (const auto &[key, ignored] : settings.items()) {
        (void)ignored;
        require(allowed.contains(key), ErrorCode::InvalidProfile, "unknown core key: " + key);
      }
      auto p = profile;
      if (settings.contains("profile_file"))
        apply_profile(
            p, read_json(relative_to(config_base, settings["profile_file"], "profile_file")));
      if (settings.contains("profile"))
        apply_profile(p, settings["profile"]);
      const auto model = settings.contains("cpu_model")
                             ? json_string(settings["cpu_model"], "cpu_model")
                             : cpu_model;
      cores.push_back({json_word(settings["id"], "core id"), p,
                       load_image(relative_to(config_base, settings["program"], "program")),
                       cpu_factory(model),
                       settings.contains("sync_capacity")
                           ? json_word(settings["sync_capacity"], "sync_capacity")
                           : 8});
      models.push_back(model);
    }
  }

  return {std::move(cores), std::move(models)};
}
std::vector<SyncConnection> configure_connections(const Json &connection_settings) {
  std::vector<SyncConnection> connections;
  for (const auto &settings : connection_settings) {
    const std::set<std::string> allowed{"first", "second", "first_to_second", "second_to_first",
                                        "capacity"};
    require(settings.is_object(), ErrorCode::InvalidProfile, "sync connection must be an object");
    for (const auto &[key, ignored] : settings.items()) {
      (void)ignored;
      require(allowed.contains(key), ErrorCode::InvalidProfile,
              "unknown sync connection key: " + key);
    }
    for (auto key : {"first", "second", "first_to_second", "second_to_first"})
      require(settings.contains(key), ErrorCode::InvalidProfile,
              std::string("missing sync connection key: ") + key);
    connections.push_back(
        {json_word(settings["first"], "first"), json_word(settings["second"], "second"),
         json_number(settings["first_to_second"], "first_to_second"),
         json_number(settings["second_to_first"], "second_to_first"),
         settings.contains("capacity") ? json_word(settings["capacity"], "capacity") : 8});
  }

  return connections;
}
} // namespace qsbit::app
