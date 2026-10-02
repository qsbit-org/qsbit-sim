#include "qsbit/python_backend.hpp"
#include <pybind11/complex.h>
#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;
namespace qsbit {
namespace {
py::dict descriptor(const EventSpec &a) {
  const char *kind = "gate";
  if (a.kind == ActionKind::Pulse)
    kind = "pulse";
  if (a.kind == ActionKind::Acquire)
    kind = "acquire";
  if (a.kind == ActionKind::DiscriminatorArm)
    kind = "arm";
  py::dict out;
  out["kind"] = kind;
  out["operation"] = a.operation;
  out["targets"] = a.targets;
  out["port"] = a.port;
  out["amplitude"] = a.amplitude;
  out["axis"] = a.axis;
  return out;
}
py::list descriptors(std::span<const EventSpec> actions) {
  py::list out;
  for (const auto &action : actions)
    out.append(descriptor(action));
  return out;
}
} // namespace
struct PythonSession::Impl {
  std::unique_ptr<py::scoped_interpreter> interpreter;
  Impl() {
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    const auto status =
        PyConfig_SetBytesString(&config, &config.program_name, QSBIT_PYTHON_EXECUTABLE);
    if (PyStatus_Exception(status)) {
      PyConfig_Clear(&config);
      throw Fault(ErrorCode::BackendFailure, "cannot configure Python executable");
    }
    interpreter = std::make_unique<py::scoped_interpreter>(&config);
  }
};
PythonSession::PythonSession(const std::string &directory) : impl_(std::make_unique<Impl>()) {
  py::module_::import("sys").attr("path").attr("insert")(0, directory);
}
PythonSession::~PythonSession() = default;
struct PythonBackend::Impl {
  py::object backend;
  std::string options;
};
PythonBackend::PythonBackend(const std::string &module, const std::string &class_name)
    : PythonBackend(PythonBackendConfig{module + ":" + class_name, "{}"}) {}
PythonBackend::PythonBackend(const PythonBackendConfig &config) : impl_(std::make_unique<Impl>()) {
  try {
    auto json = py::module_::import("json");
    auto created = py::module_::import("qsbit_backend.registry")
                       .attr("create")(config.name, json.attr("loads")(config.options));
    impl_->backend = created[py::int_(0)];
    impl_->options = json.attr("dumps")(created[py::int_(1)]).cast<std::string>();
    for (const char *method : {"validate", "reset", "evolve", "apply", "measure"}) {
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
void PythonBackend::evolve(Tick from, Tick to, std::span<const EventSpec> drives) {
  try {
    impl_->backend.attr("evolve")(from, to, descriptors(drives));
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
void PythonBackend::apply(std::span<const EventSpec> gates) {
  try {
    impl_->backend.attr("apply")(descriptors(gates));
  } catch (const py::error_already_set &e) {
    throw Fault(ErrorCode::BackendFailure, e.what());
  }
}
std::vector<bool> PythonBackend::measure(std::span<const MeasurementReference> references) {
  try {
    py::list inputs;
    for (const auto &t : references) {
      py::dict item;
      item["epoch"] = t.epoch;
      item["measurement"] = t.measurement;
      item["target"] = t.target;
      inputs.append(item);
    }
    return impl_->backend.attr("measure")(inputs).cast<std::vector<bool>>();
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
