add_executable(decoder_tests tests/decoder_tests.cpp)
target_link_libraries(decoder_tests PRIVATE qsbit_core)
add_test(NAME decoder.transport COMMAND decoder_tests)
set_tests_properties(decoder.transport PROPERTIES TIMEOUT 15 LABELS "fast;component")
add_executable(backend_execution_tests tests/backend_execution_tests.cpp)
target_link_libraries(backend_execution_tests PRIVATE qsbit_core)
add_test(NAME backend.execution COMMAND backend_execution_tests)
set_tests_properties(backend.execution PROPERTIES TIMEOUT 15 LABELS "fast;component")
add_executable(two_qubit_tests tests/two_qubit_tests.cpp)
target_link_libraries(two_qubit_tests PRIVATE qsbit_core)
add_test(NAME device.two_qubit COMMAND two_qubit_tests)
set_tests_properties(device.two_qubit PROPERTIES TIMEOUT 15 LABELS "fast;component")
add_executable(config_tests tests/config_tests.cpp)
target_include_directories(config_tests PRIVATE src)
target_link_libraries(config_tests PRIVATE qsbit_config)
add_test(NAME config.profile COMMAND config_tests)
set_tests_properties(config.profile PROPERTIES TIMEOUT 15 LABELS "fast;unit")
add_executable(run_config_tests tests/run_config_tests.cpp)
target_include_directories(run_config_tests PRIVATE src)
target_link_libraries(run_config_tests PRIVATE qsbit_app)
add_test(NAME config.run COMMAND run_config_tests "${CMAKE_CURRENT_BINARY_DIR}/run-config")
set_tests_properties(config.run PROPERTIES TIMEOUT 15 LABELS "fast;unit")
add_executable(sync_tests tests/sync_tests.cpp)
target_link_libraries(sync_tests PRIVATE qsbit_core)
add_test(NAME sync.neighbor COMMAND sync_tests)
set_tests_properties(sync.neighbor PROPERTIES TIMEOUT 15 LABELS "fast;unit")
add_executable(vliw_tests tests/vliw_tests.cpp)
target_link_libraries(vliw_tests PRIVATE qsbit_core)
add_test(NAME cpu.vliw COMMAND vliw_tests)
set_tests_properties(cpu.vliw PROPERTIES TIMEOUT 15 LABELS "fast;unit")
add_executable(cpu_trace_tests tests/cpu_trace_tests.cpp)
target_link_libraries(cpu_trace_tests PRIVATE qsbit_core)
add_test(NAME cpu.trace COMMAND cpu_trace_tests)
set_tests_properties(cpu.trace PROPERTIES TIMEOUT 15 LABELS "fast;unit")
add_executable(control_isa_tests tests/control_isa_tests.cpp)
target_link_libraries(control_isa_tests PRIVATE qsbit_core)
add_test(NAME control.instructions COMMAND control_isa_tests)
set_tests_properties(control.instructions PROPERTIES TIMEOUT 15 LABELS "fast;unit")
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE qsbit_core)
foreach(scenario isa_arithmetic isa_control isa_decode image mailbox memory trace_filter)
  add_test(NAME core.${scenario} COMMAND core_tests ${scenario})
  set_tests_properties(core.${scenario} PROPERTIES TIMEOUT 15 LABELS "fast;unit")
endforeach()
add_executable(control_tests tests/control_tests.cpp)
add_executable(wait_tests tests/wait_tests.cpp)
target_link_libraries(wait_tests PRIVATE qsbit_core)
add_test(NAME control.wait_zero COMMAND wait_tests)
set_tests_properties(control.wait_zero PROPERTIES TIMEOUT 15 LABELS "fast;component")
target_link_libraries(control_tests PRIVATE qsbit_core)
foreach(scenario admission atomic empty registers fast mapping)
  add_test(NAME control.${scenario} COMMAND control_tests ${scenario})
  set_tests_properties(control.${scenario} PROPERTIES TIMEOUT 15 LABELS "fast;component")
endforeach()
add_executable(protocol_tests tests/protocol_tests.cpp)
target_link_libraries(protocol_tests PRIVATE qsbit_core)
foreach(scenario timing capacity resources readout overflow reset sample_collision flags)
  add_test(NAME protocol.${scenario} COMMAND protocol_tests ${scenario})
  set_tests_properties(protocol.${scenario} PROPERTIES TIMEOUT 15 LABELS "fast;component")
endforeach()
add_executable(tcu_trace tests/tcu_trace.cpp)
target_link_libraries(tcu_trace PRIVATE qsbit_core SystemC::systemc)
add_test(NAME systemc.tcu_trace COMMAND tcu_trace "${CMAKE_CURRENT_BINARY_DIR}/tcu-trace.jsonl"
  1000 1 2 3 4 8 16)
set_tests_properties(systemc.tcu_trace PROPERTIES TIMEOUT 15 LABELS "fast;integration")
find_program(RISCV_OBJDUMP NAMES riscv64-unknown-elf-objdump riscv64-elf-objdump REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(INSTALL_TEST_OPTIONS)
if(QSBIT_PYTHON_BACKENDS)
  list(APPEND INSTALL_TEST_OPTIONS --python)
endif()
add_test(NAME cli.install COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/install.py"
  --build "${CMAKE_CURRENT_BINARY_DIR}" --bindir "${CMAKE_INSTALL_BINDIR}"
  --program "${EXAMPLE_IMAGE_bell}" ${INSTALL_TEST_OPTIONS})
set_tests_properties(cli.install PROPERTIES TIMEOUT 60 LABELS "fast;integration")
if(QSBIT_TEST_QEC)
  add_test(NAME decoder.pymatching COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/decoding.py")
  set_tests_properties(decoder.pymatching PROPERTIES TIMEOUT 30 LABELS "backend;component")
  if(QSBIT_BLOQ_EXPORTER AND QSBIT_QIR_COMPILER)
    foreach(format bc ll)
      if(format STREQUAL "bc")
        set(bloq_test bitcode)
        set(bloq_latency 1000)
      else()
        set(bloq_test text)
        set(bloq_latency 100000)
      endif()
      add_test(NAME integration.bloq_${bloq_test} COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/bloq-qir/run.py"
        --exporter "${QSBIT_BLOQ_EXPORTER}" --compiler "${QSBIT_QIR_COMPILER}"
        --sim "$<TARGET_FILE:qsbit_sim>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/bloq-qir-${bloq_test}"
        --shots 1 --qir-format "${format}" --decoder-latency "${bloq_latency}")
      set_tests_properties(integration.bloq_${bloq_test} PROPERTIES
        TIMEOUT 180 LABELS "numerical;backend;integration")
    endforeach()
  endif()
endif()
add_test(NAME systemc.vliw COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/vliw.py" --simulator "$<TARGET_FILE:qsbit_sim>"
  --assembler "${RISCV_AS}" --linker "${RISCV_LD}" --source "${CMAKE_CURRENT_SOURCE_DIR}"
  --output "${CMAKE_CURRENT_BINARY_DIR}/vliw-test")
set_tests_properties(systemc.vliw PROPERTIES TIMEOUT 60 LABELS "fast;integration")
add_test(NAME build.dependencies COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/dependency_setup.py"
  --cmake "${CMAKE_COMMAND}" --source "${CMAKE_CURRENT_SOURCE_DIR}"
  --toolchain "${CMAKE_TOOLCHAIN_FILE}" --configuration "${CMAKE_BUILD_TYPE}")
set_tests_properties(build.dependencies PROPERTIES TIMEOUT 120 LABELS "fast;integration")
add_test(NAME docs.contracts COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_docs.py" --build "${CMAKE_CURRENT_BINARY_DIR}"
  --ctest "${CMAKE_CTEST_COMMAND}")
add_test(NAME docs.checker COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/documentation.py")
set_tests_properties(docs.contracts docs.checker PROPERTIES TIMEOUT 30 LABELS "fast;documentation")
add_test(NAME trace.replay COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/trace_replay.py")
set_tests_properties(trace.replay PROPERTIES TIMEOUT 30 LABELS "fast;documentation")
add_test(NAME systemc.distributed COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/distributed.py" --simulator "$<TARGET_FILE:qsbit_sim>"
  --assembler "${RISCV_AS}" --linker "${RISCV_LD}" --output "${CMAKE_CURRENT_BINARY_DIR}/distributed-test")
set_tests_properties(systemc.distributed PROPERTIES TIMEOUT 60 LABELS "fast;integration")
if(QSBIT_TEST_AER)
  add_test(NAME numerical.distributed COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/distributed.py" --simulator "$<TARGET_FILE:qsbit_sim>"
    --assembler "${RISCV_AS}" --linker "${RISCV_LD}" --output "${CMAKE_CURRENT_BINARY_DIR}/distributed-aer"
    --backend aer)
  set_tests_properties(numerical.distributed PROPERTIES TIMEOUT 120 LABELS "numerical;integration")
endif()
if(QSBIT_TEST_WEBSITE)
  add_test(NAME docs.website_build COMMAND "${Python3_EXECUTABLE}" -m sphinx
    -n -W --keep-going -b html -D "qsbit_build=${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs" "${CMAKE_CURRENT_BINARY_DIR}/docs/html")
  add_test(NAME docs.website_browser COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/website.py"
    --site "${CMAKE_CURRENT_BINARY_DIR}/docs/html")
  set_tests_properties(docs.website_build PROPERTIES TIMEOUT 300 LABELS "website"
    FIXTURES_SETUP documentation_site)
  set_tests_properties(docs.website_browser PROPERTIES TIMEOUT 120 LABELS "website"
    FIXTURES_REQUIRED documentation_site)
endif()
add_test(NAME systemc.use_cases COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/use_cases.py" --simulator "$<TARGET_FILE:qsbit_sim>"
  --assembler "${RISCV_AS}" --linker "${RISCV_LD}" --objdump "${RISCV_OBJDUMP}"
  --source "${CMAKE_CURRENT_SOURCE_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/use-cases")
set_tests_properties(systemc.use_cases PROPERTIES TIMEOUT 120 LABELS "fast;integration")
add_test(NAME systemc.vliw_use_cases COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/use_cases.py" --simulator "$<TARGET_FILE:qsbit_sim>"
  --cpu-model vliw
  --assembler "${RISCV_AS}" --linker "${RISCV_LD}" --objdump "${RISCV_OBJDUMP}"
  --source "${CMAKE_CURRENT_SOURCE_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/vliw-use-cases")
set_tests_properties(systemc.vliw_use_cases PROPERTIES TIMEOUT 120 LABELS "fast;integration")
add_test(NAME cli.mock COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_configs.py" --simulator "$<TARGET_FILE:qsbit_sim>"
  --build "${CMAKE_CURRENT_BINARY_DIR}" --scenarios mock)
set_tests_properties(cli.mock PROPERTIES TIMEOUT 30 LABELS "fast;integration")
add_executable(adapter_tests tests/adapter_tests.cpp)
target_link_libraries(adapter_tests PRIVATE qsbit_systemc)
foreach(scenario normal reset fine_resolution coarse_resolution)
  add_test(NAME adapter.${scenario} COMMAND adapter_tests ${scenario})
  set_tests_properties(adapter.${scenario} PROPERTIES TIMEOUT 15 LABELS "fast;integration")
endforeach()
add_executable(system_tests tests/system_tests.cpp)
target_link_libraries(system_tests PRIVATE qsbit_systemc)
add_dependencies(system_tests example_images)
if(QSBIT_ISA_REFERENCES)
  add_test(NAME reference.rv32_random COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/random_isa.py" --assembler "${RISCV_AS}" --linker "${RISCV_LD}"
    --script "${CMAKE_CURRENT_SOURCE_DIR}/examples/common/link.ld" --simulator "$<TARGET_FILE:qsbit_sim>"
    --output "${CMAKE_CURRENT_BINARY_DIR}/random-isa")
  set_tests_properties(reference.rv32_random PROPERTIES TIMEOUT 120 LABELS "reference;randomized")
  if(QSBIT_ARCH_TEST_SOURCE)
    add_test(NAME reference.rv32_architecture COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture_isa.py" --suite "${QSBIT_ARCH_TEST_SOURCE}"
      --assembler "${RISCV_AS}" --linker "${RISCV_LD}" --script "${CMAKE_CURRENT_SOURCE_DIR}/examples/common/link.ld"
      --simulator "$<TARGET_FILE:qsbit_sim>" --adapter "${CMAKE_CURRENT_SOURCE_DIR}/tests/arch"
      --output "${CMAKE_CURRENT_BINARY_DIR}/architecture-isa")
    set_tests_properties(reference.rv32_architecture PROPERTIES TIMEOUT 600 LABELS "reference;architecture")
  endif()
endif()
foreach(scenario bell feedback_one feedback_zero)
  string(REGEX REPLACE "_.*" "" example "${scenario}")
  foreach(order normal reverse)
    add_test(NAME systemc.${scenario}.${order} COMMAND system_tests "${scenario}"
      "${EXAMPLE_IMAGE_${example}}" "${order}"
      "${CMAKE_CURRENT_BINARY_DIR}/${scenario}-${order}.jsonl")
    set_tests_properties(systemc.${scenario}.${order} PROPERTIES TIMEOUT 15 LABELS "fast;integration")
  endforeach()
endforeach()
if(QSBIT_PYTHON_BACKENDS)
  if(QSBIT_TEST_QUTIP)
    add_test(NAME python.qutip COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/qutip_backend.py"
      --simulator "$<TARGET_FILE:qsbit_sim>" --assembler "${RISCV_AS}" --linker "${RISCV_LD}")
    set_tests_properties(python.qutip PROPERTIES TIMEOUT 120 LABELS "numerical;backend;integration")
  endif()
  if(QSBIT_TEST_EXPERIMENTS)
    add_test(NAME python.repetition COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/repetition.py" --simulator "$<TARGET_FILE:qsbit_sim>"
      --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}"
      --assembler "${RISCV_AS}" --linker "${RISCV_LD}")
    set_tests_properties(python.repetition PROPERTIES TIMEOUT 180 LABELS "numerical;integration")
    add_test(NAME experiment.allxy COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/allxy.py" --simulator "$<TARGET_FILE:qsbit_sim>"
      --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(experiment.allxy PROPERTIES TIMEOUT 180 LABELS "experiment;numerical;integration")
  endif()
  set(BACKEND_TEST_OPTIONS "")
  if(QSBIT_TEST_AER)
    list(APPEND BACKEND_TEST_OPTIONS --aer)
  endif()
  if(QSBIT_TEST_STIM)
    list(APPEND BACKEND_TEST_OPTIONS --stim)
  endif()
  if(QSBIT_TEST_AER OR QSBIT_TEST_STIM)
    set(SEMANTICS_OPTIONS ${BACKEND_TEST_OPTIONS})
    add_test(NAME python.backend_semantics COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend_semantics.py" ${SEMANTICS_OPTIONS})
    set_tests_properties(python.backend_semantics PROPERTIES TIMEOUT 60 LABELS "numerical;backend")
  endif()
  add_test(NAME python.backend_configuration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/backend_configuration.py"
    --simulator "$<TARGET_FILE:qsbit_sim>" --build "${CMAKE_CURRENT_BINARY_DIR}"
    ${BACKEND_TEST_OPTIONS})
  set_tests_properties(python.backend_configuration PROPERTIES TIMEOUT 120 LABELS "integration;backend")
  add_test(NAME python.plugin COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_plugin.py" --simulator "$<TARGET_FILE:qsbit_sim>"
    --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(python.plugin PROPERTIES TIMEOUT 30 LABELS "fast;integration")
endif()
if(QSBIT_TEST_AER)
  set(NUMERICAL_SCENARIOS bell feedback capability final_state)
  add_test(NAME reference.cactus_golden COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cactus_golden.py"
    --simulator "$<TARGET_FILE:qsbit_sim>"
    --assembler "${RISCV_AS}" --linker "${RISCV_LD}"
    --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(reference.cactus_golden PROPERTIES TIMEOUT 600
    LABELS "reference;integration;numerical")
  add_test(NAME cli.examples COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_configs.py" --simulator "$<TARGET_FILE:qsbit_sim>"
    --build "${CMAKE_CURRENT_BINARY_DIR}"
    --scenarios bell feedback)
  set_tests_properties(cli.examples PROPERTIES TIMEOUT 120 LABELS "numerical;integration")
  add_executable(numerical_tests tests/numerical_tests.cpp)
  target_link_libraries(numerical_tests PRIVATE qsbit_systemc qsbit_python)
  target_compile_definitions(numerical_tests PRIVATE QSBIT_PYTHON_MODULE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/python")
  add_dependencies(numerical_tests example_images)
  foreach(scenario IN LISTS NUMERICAL_SCENARIOS)
    add_test(NAME numerical.${scenario} COMMAND numerical_tests "${scenario}"
      "${EXAMPLE_IMAGE_${scenario}}")
    set_tests_properties(numerical.${scenario} PROPERTIES TIMEOUT 120 LABELS "numerical;integration")
  endforeach()
endif()
