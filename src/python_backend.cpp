#include "qsbit/python_backend.hpp"
#include <cstdlib>
#include <filesystem>
#include <pybind11/complex.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;
namespace qsbit {
DecoderSystemConfig python_decoders(const std::string &config) {
  auto module = py::module_::import("qsbit_backend.decoding");
  auto settings = module.attr("validate")(py::module_::import("json").attr("loads")(config));
  DecoderSystemConfig result;
  result.base = settings["mmio_base"].cast<std::uint32_t>();
  result.request_capacity = settings["request_capacity"].cast<std::uint32_t>();
  result.result_capacity = settings["result_capacity"].cast<std::uint32_t>();
  result.link_latency = settings["link_latency"].cast<Tick>();
  result.bytes_per_tick = settings["bytes_per_tick"].cast<std::uint32_t>();
  result.packet_overhead = settings["packet_overhead"].cast<std::uint32_t>();
  for (auto item : settings["decoders"]) {
    auto d = py::reinterpret_borrow<py::dict>(item);
    auto callback = module.attr("create")(d);
    result.decoders.push_back(
        {d["id"].cast<std::uint32_t>(), d["measurements"].cast<std::uint32_t>(),
         d["outputs"].cast<std::uint32_t>(), d["latency"].cast<Tick>(),
         d["initiation_interval"].cast<Tick>(), [callback](std::span<const std::uint8_t> bits) {
           return callback(std::vector<std::uint8_t>(bits.begin(), bits.end()))
               .cast<std::vector<bool>>();
         }});
  }
  return result;
}
namespace {
py::dict descriptor(const EventSpec &a) {
  const char *kind = "gate";
  if (a.kind() == ActionKind::Pulse)
    kind = "pulse";
  if (a.kind() == ActionKind::Acquire)
    kind = "acquire";
  if (a.kind() == ActionKind::DiscriminatorArm)
    kind = "arm";
  py::dict out;
  out["kind"] = kind;
  out["operation"] = a.operation();
  out["targets"] = a.targets();
  std::visit(
      [&](const auto &value) {
        if constexpr (requires { value.amplitude; })
          out["amplitude"] = value.amplitude;
        if constexpr (requires { value.axis; }) {
          out["axis"] = value.axis;
          out["port"] = a.port;
        }
      },
      a.spec);
  return out;
}
py::list descriptors(std::span<const EventSpec> actions) {
  py::list out;
  for (const auto &action : actions)
    out.append(descriptor(action));
  return out;
}
py::list activities(std::span<const BackendActivity> active) {
  py::list out;
  for (const auto &activity : active) {
    auto item = descriptor(activity.action);
    item["id"] = activity.id;
    item["start"] = activity.start;
    item["end"] = activity.end;
    item["port"] = activity.action.port;
    if (activity.reference)
      item["measurement"] = activity.reference->measurement;
    out.append(std::move(item));
  }
  return out;
}
} // namespace
struct PythonSession::Impl {
  std::unique_ptr<py::scoped_interpreter> interpreter;
  Impl() {
    std::string executable =
        "python" + std::to_string(PY_MAJOR_VERSION) + "." + std::to_string(PY_MINOR_VERSION);
    if (const auto *selected = std::getenv("QSBIT_PYTHON"); selected && *selected)
      executable = selected;
    else if (const auto *environment = std::getenv("VIRTUAL_ENV"); environment && *environment)
      executable = (std::filesystem::path(environment) / "bin/python").string();
    if (std::filesystem::path(executable).has_parent_path())
      require(std::filesystem::is_regular_file(executable), ErrorCode::BackendFailure,
              "Python interpreter does not exist: " + executable);
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    const auto status = PyConfig_SetBytesString(&config, &config.program_name, executable.c_str());
    if (PyStatus_Exception(status)) {
      PyConfig_Clear(&config);
      throw Fault(ErrorCode::BackendFailure, "cannot configure Python executable");
    }
    interpreter = std::make_unique<py::scoped_interpreter>(&config, 0, nullptr, false);
  }
};
PythonSession::PythonSession(const std::string &directory) : impl_(std::make_unique<Impl>()) {
  if (!directory.empty())
    py::module_::import("sys").attr("path").attr("insert")(0, directory);
}
PythonSession::~PythonSession() = default;
struct PythonBackend::Impl {
  py::object backend;
  std::string options;
};
PythonBackend::PythonBackend(const PythonBackendConfig &config) : impl_(std::make_unique<Impl>()) {
  try {
    auto json = py::module_::import("json");
    auto created = py::module_::import("qsbit_backend.registry")
                       .attr("create")(config.name, json.attr("loads")(config.options));
    impl_->backend = created[py::int_(0)];
    impl_->options = json.attr("dumps")(created[py::int_(1)]).cast<std::string>();
    for (const char *method : {"validate", "reset", "execute"}) {
      require(py::hasattr(impl_->backend, method) &&
                  PyCallable_Check(impl_->backend.attr(method).ptr()),
              ErrorCode::BackendFailure, std::string("backend requires callable ") + method);
    }
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
PythonBackend::~PythonBackend() = default;
std::string PythonBackend::inspect(const std::string &name, const std::string &command) {
  try {
    return py::module_::import("qsbit_backend.registry")
        .attr("inspect_backend")(name, command)
        .cast<std::string>();
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
std::string PythonBackend::options() const { return impl_->options; }
void PythonBackend::run_simulation(const std::string &config, const std::string &executable,
                                   bool check_only) {
  try {
    py::module_::import("qsbit_backend.simulation")
        .attr("run_config")(config, executable, check_only);
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
void PythonBackend::validate(const EventSpec &action) const {
  try {
    impl_->backend.attr("validate")(descriptor(action));
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::UnsupportedCapability, e.what());
  }
}
void PythonBackend::reset(std::uint32_t qubits, std::uint32_t seed) {
  try {
    impl_->backend.attr("reset")(qubits, seed);
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
std::vector<bool> PythonBackend::execute(Epoch epoch,
                                         std::span<const BackendOperation> operations) {
  try {
    py::list inputs;
    for (const auto &operation : operations) {
      py::dict item;
      std::visit(
          [&](const auto &value) {
            item["tick"] = value.tick;
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, BackendEvolution>) {
              item["kind"] = "evolve";
              item["start"] = value.start;
              item["drives"] = activities(value.drives);
              item["acquisitions"] = activities(value.acquisitions);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, BackendGates>) {
              item["kind"] = "apply";
              item["gates"] = descriptors(value.gates);
            } else {
              item["kind"] = "measure";
              py::list references;
              for (const auto &reference : value.references) {
                py::dict entry;
                entry["epoch"] = reference.epoch;
                entry["measurement"] = reference.measurement;
                entry["target"] = reference.target;
                references.append(std::move(entry));
              }
              item["references"] = references;
            }
          },
          operation);
      inputs.append(item);
    }
    return impl_->backend.attr("execute")(epoch, inputs).cast<std::vector<bool>>();
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
std::vector<std::complex<double>> PythonBackend::state() const {
  try {
    if (!py::hasattr(impl_->backend, "state"))
      return {};
    return impl_->backend.attr("state")().cast<std::vector<std::complex<double>>>();
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
std::vector<std::vector<std::complex<double>>> PythonBackend::density_matrix() const {
  try {
    if (!py::hasattr(impl_->backend, "density_matrix"))
      return {};
    return impl_->backend.attr("density_matrix")()
        .cast<std::vector<std::vector<std::complex<double>>>>();
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
} // namespace qsbit
