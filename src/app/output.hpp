#pragma once
#include "config.hpp"
#include "qsbit/simulator.hpp"

namespace qsbit::app {
Json simulation_summary(Simulator &sim, const std::string &backend_name,
                        const Json &backend_options, BackendExecutionConfig backend_execution,
                        const std::vector<std::string> &models,
                        std::span<const SyncConnection> connections,
                        std::span<const std::uint32_t> inspect, bool multicore);
}
