#include "output.hpp"

namespace qsbit::app {
namespace {
Json core_summary(const Core &core, const std::string &model) {
  Json registers = Json::array();
  for (const auto &reg : core.measurement_registers().registers())
    registers.push_back(
        {{"pending", reg.pending}, {"valid", reg.pending == 0}, {"value", reg.value}});
  return {{"cpu_model", model},
          {"configuration", profile_json(core.profile())},
          {"registers", core.cpu().registers()},
          {"pc", core.cpu().pc()},
          {"exit_code", core.cpu().halted() ? Json(core.cpu().registers()[10]) : Json(nullptr)},
          {"measurement_registers", std::move(registers)}};
}
} // namespace
Json simulation_summary(Simulator &sim, const std::string &backend_name,
                        const Json &backend_options, BackendExecutionConfig backend_execution,
                        const std::vector<std::string> &models, const Json &connection_settings,
                        std::span<const std::uint32_t> inspect, bool multicore) {
  Json result{
      {"schema", 1},
      {"success", sim.success()},
      {"stop_tick", sc_core::sc_time_stamp().value()},
      {"backend", backend_name},
      {"backend_options", backend_options},
      {"backend_execution", {{"max_batch_operations", backend_execution.max_batch_operations}}},
      {"statevector", Json::array()},
      {"memory", Json::object()}};
  if (sim.fault()) {
    result["fault"] = qsbit::name(*sim.fault());
    result["message"] = sim.fault_message();
  }
  if (multicore) {
    result["cores"] = Json::array();
    for (std::size_t i = 0; i < sim.core_count(); ++i) {
      const auto &core = sim.core(i);
      auto entry = core_summary(core, models[i]);
      entry["id"] = core.id();
      entry["drained"] = core.drained();
      result["cores"].push_back(std::move(entry));
    }
    result["sync_connections"] = connection_settings;
  } else {
    result.update(core_summary(sim.core(0), models.front()));
    result["configuration_hash"] = sim.core(0).profile().fingerprint();
  }
  for (const auto &amplitude : sim.backend().state())
    result["statevector"].push_back({amplitude.real(), amplitude.imag()});
  const auto density = sim.backend().density_matrix();
  if (!density.empty()) {
    result["density_matrix"] = Json::array();
    for (const auto &row : density) {
      Json values = Json::array();
      for (const auto &entry : row)
        values.push_back({entry.real(), entry.imag()});
      result["density_matrix"].push_back(std::move(values));
    }
  }
  for (auto address : inspect)
    result["memory"][std::to_string(address)] = sim.memory().read(address, 4);
  return result;
}
} // namespace qsbit::app
