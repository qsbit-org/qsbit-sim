#include "classical_pipeline.h"
#include "global_counter.h"
#include "qvm.h"
#include "timing_control_unit.h"
#include <fstream>

using namespace cactus;
using namespace sc_core;

struct FullHarness : sc_module {
  sc_in<bool> fast, slow;
  sc_signal<bool> reset, init, run, started, done, empty;
  sc_signal<sc_dt::sc_uint<MEMORY_ADDRESS_WIDTH>> pc;
  QVM qvm{"qvm"};
  sc_signal<Ops_2_qsim> *operations = nullptr;
  sc_signal<Res_from_qsim> *results = nullptr;
  sc_signal<Generic_meas_if> *feedback = nullptr;
  Event_queue_manager *queue = nullptr;
  Classical_pipeline *cpu = nullptr;
  std::vector<bool> old_valid, result_pending;
  std::ofstream trace;
  SC_HAS_PROCESS(FullHarness);
  FullHarness(sc_module_name name, const char *path) : sc_module(name), trace(path) {
    qvm.clock(fast);
    qvm.clock_50MHz(slow);
    qvm.reset(reset);
    qvm.init(init);
    qvm.run(run);
    qvm.App2Clp_init_pc(pc);
    qvm.Clp2App_done(done);
    qvm.Qp2App_eq_empty(empty);
    global_counter::register_counter(fast, started, "cycle_counter_200MHz");
    global_counter::register_counter(slow, run, "cycle_counter_50MHz");
    for (auto *object : qvm.get_child_objects()) {
      if (auto *typed = dynamic_cast<sc_signal<Ops_2_qsim> *>(object))
        operations = typed;
      if (auto *typed = dynamic_cast<sc_signal<Res_from_qsim> *>(object))
        results = typed;
      if (auto *typed = dynamic_cast<sc_signal<Generic_meas_if> *>(object))
        feedback = typed;
    }
    queue = dynamic_cast<Event_queue_manager *>(sc_find_object(
        "harness.qvm.cclight.quantum.q_ppl_dep.timing_control_unit.event_queue_manager"));
    if (!operations || !results || !feedback || !queue)
      throw std::runtime_error("required reference probe is unavailable");
    cpu = dynamic_cast<Classical_pipeline *>(
        sc_find_object("harness.qvm.cclight.classical.Classical_pipeline"));
    if (!cpu)
      throw std::runtime_error("CPU visibility probe is unavailable");
    old_valid.resize(cpu->MRF2Clp_valid.size());
    result_pending.resize(cpu->MRF2Clp_valid.size());
    SC_METHOD(observe_cpu);
    for (auto &port : cpu->MRF2Clp_valid)
      sensitive << port;
    dont_initialize();
    SC_THREAD(drive);
    SC_METHOD(observe_operations);
    sensitive << *operations;
    dont_initialize();
    SC_METHOD(observe_results);
    sensitive << *results;
    dont_initialize();
    SC_METHOD(observe_feedback);
    sensitive << *feedback;
    dont_initialize();
    SC_METHOD(observe_tcu);
    sensitive << queue->out_q_pipe_interface;
    dont_initialize();
    SC_METHOD(observe_error);
    sensitive << empty;
    dont_initialize();
  }
  void observe_cpu() {
    for (size_t i = 0; i < cpu->MRF2Clp_valid.size(); ++i) {
      const bool valid = cpu->MRF2Clp_valid[i].read() != 0;
      if (valid && !old_valid[i] && result_pending[i]) {
        trace << "{\"kind\":\"CpuResultVisible\",\"tick\":" << sc_time_stamp().value()
              << ",\"target\":" << i << ",\"value\":" << cpu->MRF2Clp_data[i].read() << "}\n";
        result_pending[i] = false;
      }
      old_valid[i] = valid;
    }
  }
  void drive() {
    reset.write(true);
    started.write(true);
    run.write(false);
    pc.write(0);
    wait(11, SC_NS);
    reset.write(false);
    wait(10, SC_NS);
    init.write(true);
    wait(10, SC_NS);
    init.write(false);
    wait(sc_time(999, SC_NS) - sc_time_stamp());
    run.write(true);
    trace << "{\"kind\":\"RunPublished\",\"tick\":999}\n";
  }
  void observe_operations() {
    const auto &moment = operations->read();
    if (!moment.triggered)
      return;
    for (const auto &op : moment.atom_ops) {
      trace << "{\"kind\":\"DeviceCommand\",\"tick\":" << sc_time_stamp().value()
            << ",\"cycle\":" << moment.cycle << ",\"operation\":\"" << op.operation
            << "\",\"targets\":[";
      for (size_t i = 0; i < op.target_qubits.size(); ++i) {
        if (i)
          trace << ',';
        trace << op.target_qubits[i];
      }
      trace << "]}\n";
    }
  }
  void observe_results() {
    for (const auto &result : results->read().results) {
      result_pending.at(result.first) = true;
      trace << "{\"kind\":\"ResultReady\",\"tick\":" << sc_time_stamp().value()
            << ",\"target\":" << result.first << ",\"value\":" << result.second << "}\n";
    }
  }
  void observe_feedback() {
    auto f = feedback->read();
    const auto valid = f.get_meas_data_valid();
    const auto values = f.get_meas_data();
    for (size_t i = 0; i < valid.size(); ++i)
      if (valid[i])
        trace << "{\"kind\":\"FeedbackPublished\",\"tick\":" << sc_time_stamp().value()
              << ",\"target\":" << i << ",\"value\":" << values[i] << "}\n";
  }
  void observe_tcu() {
    const auto &group = queue->out_q_pipe_interface.read();
    if (group.if_content.valid_wait)
      trace << "{\"kind\":\"TcuOutput\",\"tick\":" << sc_time_stamp().value()
            << ",\"label\":" << group.timing.label << ",\"interval\":" << group.timing.wait_time
            << "}\n";
  }
  void observe_error() {
    if (empty.read())
      trace << "{\"kind\":\"QueueError\",\"tick\":" << sc_time_stamp().value() << "}\n";
  }
};
int sc_main(int argc, char **argv) {
  sc_set_time_resolution(1, SC_NS);
  auto &config = Global_config::get_instance();
  config.init_cmdparser(argc, argv);
  config.run_cmdparser();
  spdlog::set_level(spdlog::level::err);
  const char *output = std::getenv("CACTUS_TRACE");
  if (!output)
    return 2;
  sc_clock fast("fast", 5, SC_NS), slow("slow", 20, SC_NS);
  FullHarness harness("harness", output);
  harness.fast(fast);
  harness.slow(slow);
  sc_start(12000, SC_NS);
  return 0;
}
