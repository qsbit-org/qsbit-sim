cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  message(FATAL_ERROR
    "Conan dependencies are not prepared for this preset. "
    "Follow docs/prerequisites.md, then configure with a CMake preset.")
endif()
project(qsbit_sim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(QSBIT_COVERAGE "Instrument project code for gcov coverage" OFF)
option(QSBIT_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
option(QSBIT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(QSBIT_PYTHON_BACKENDS "Build the optional Python backend bridge" OFF)
option(QSBIT_BUILD_EXAMPLES "Assemble the example programs" ON)
option(BUILD_TESTING "Build and register tests" OFF)
option(QSBIT_TEST_AER "Run tests requiring the Aer extra" OFF)
option(QSBIT_TEST_PULSE "Run tests requiring the pulse extra" OFF)
option(QSBIT_TEST_STIM "Run tests requiring the Stim extra" OFF)
option(QSBIT_TEST_EXPERIMENTS "Run full and replay experiment checks" OFF)
option(QSBIT_TEST_WEBSITE "Build and browser-test the documentation website" OFF)
option(QSBIT_ISA_REFERENCES "Run optional independent RV32I reference tests" OFF)
set(QSBIT_ARCH_TEST_SOURCE "" CACHE PATH "Provisioned, pinned riscv-arch-test checkout")

include(cmake/SystemC.cmake)

if(BUILD_TESTING OR QSBIT_PYTHON_BACKENDS)
  if(DEFINED ENV{VIRTUAL_ENV} AND NOT Python3_EXECUTABLE)
    find_program(Python3_EXECUTABLE NAMES python python3
      PATHS "$ENV{VIRTUAL_ENV}/bin" "$ENV{VIRTUAL_ENV}/Scripts"
      NO_DEFAULT_PATH REQUIRED)
  endif()
endif()

add_library(qsbit_warnings INTERFACE)
target_compile_options(qsbit_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
if(QSBIT_WARNINGS_AS_ERRORS)
  target_compile_options(qsbit_warnings INTERFACE -Werror)
endif()
if(QSBIT_COVERAGE)
  if(QSBIT_SANITIZERS)
    message(FATAL_ERROR "Use separate sanitizer and coverage builds")
  endif()
  target_compile_options(qsbit_warnings INTERFACE --coverage -O0 -g)
  target_link_options(qsbit_warnings INTERFACE --coverage)
endif()
if(QSBIT_SANITIZERS)
  target_compile_options(qsbit_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(qsbit_warnings INTERFACE -fsanitize=address,undefined)
endif()

# No SystemC or Python dependency is attached to the pure transition library.
add_library(qsbit_core STATIC src/error.cpp src/isa.cpp src/image.cpp src/memory.cpp
  src/control.cpp src/trace.cpp src/feedback.cpp src/tcu.cpp src/timing_control.cpp src/cpu/rv32.cpp
  src/backend.cpp src/device.cpp src/defaults.cpp src/cpu/vliw.cpp)
target_include_directories(qsbit_core PUBLIC include)
target_link_libraries(qsbit_core PUBLIC qsbit_warnings)
add_library(qsbit_systemc STATIC src/simulator.cpp)
target_link_libraries(qsbit_systemc PUBLIC qsbit_core SystemC::systemc)
if(QSBIT_PYTHON_BACKENDS)
  set(Python3_FIND_VIRTUALENV FIRST)
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
  execute_process(COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  set(pybind11_DIR "${PYBIND11_CMAKE_DIR}")
  find_package(pybind11 3 CONFIG REQUIRED PATHS "${PYBIND11_CMAKE_DIR}" NO_DEFAULT_PATH)
  add_library(qsbit_python STATIC src/python_backend.cpp)
  set_target_properties(qsbit_python PROPERTIES CXX_VISIBILITY_PRESET hidden)
  target_compile_definitions(qsbit_python PRIVATE QSBIT_PYTHON_EXECUTABLE="${Python3_EXECUTABLE}")
  target_link_libraries(qsbit_python PUBLIC qsbit_core PRIVATE pybind11::embed)
endif()

add_library(qsbit_config STATIC src/config_json.cpp)
target_include_directories(qsbit_config SYSTEM PUBLIC third_party/nlohmann_json)
target_link_libraries(qsbit_config PUBLIC qsbit_core)
add_executable(qsbit_sim src/main.cpp)
set_target_properties(qsbit_sim PROPERTIES OUTPUT_NAME qsbit-sim)
target_link_libraries(qsbit_sim PRIVATE qsbit_systemc qsbit_config)
target_compile_definitions(qsbit_sim PRIVATE QSBIT_PYTHON_MODULE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/python")
if(QSBIT_PYTHON_BACKENDS)
  target_link_libraries(qsbit_sim PRIVATE qsbit_python)
  target_compile_definitions(qsbit_sim PRIVATE QSBIT_HAS_PYTHON=1)
endif()

if(QSBIT_TEST_WEBSITE AND NOT BUILD_TESTING)
  message(FATAL_ERROR "Website tests require BUILD_TESTING")
endif()
if(QSBIT_TEST_AER OR QSBIT_TEST_PULSE OR QSBIT_TEST_STIM OR QSBIT_TEST_EXPERIMENTS)
  if(NOT BUILD_TESTING OR NOT QSBIT_PYTHON_BACKENDS)
    message(FATAL_ERROR "Backend tests require BUILD_TESTING and QSBIT_PYTHON_BACKENDS")
  endif()
endif()
if(QSBIT_BUILD_EXAMPLES OR BUILD_TESTING)
  find_program(RISCV_AS NAMES riscv64-unknown-elf-as riscv64-elf-as REQUIRED)
  find_program(RISCV_LD NAMES riscv64-unknown-elf-ld riscv64-elf-ld REQUIRED)
  set(EXAMPLE_DIRECTORY_bell bell-state)
  set(EXAMPLE_DIRECTORY_feedback measurement-feedback)
  set(EXAMPLE_DIRECTORY_pulse single-qubit-pulse)
  set(EXAMPLE_DIRECTORY_overlap overlapping-pulses)
  foreach(example bell feedback pulse overlap)
    set(directory "${EXAMPLE_DIRECTORY_${example}}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/examples/${directory}")
    file(MAKE_DIRECTORY "${output}")
    configure_file("examples/${directory}/run.json" "${output}/run.json" COPYONLY)
    set(EXAMPLE_IMAGE_${example} "${output}/program.elf")
    add_custom_command(OUTPUT "${output}/program.elf"
      COMMAND "${RISCV_AS}" -march=rv32i -mabi=ilp32 -mno-relax
        -I "${CMAKE_CURRENT_SOURCE_DIR}/examples/common" -o "${output}/program.o"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/${directory}/program.S"
      COMMAND "${RISCV_LD}" -m elf32lriscv --no-relax -T "${CMAKE_CURRENT_SOURCE_DIR}/examples/common/link.ld"
        -o "${output}/program.elf" "${output}/program.o"
      DEPENDS "examples/${directory}/program.S" examples/common/quantum.inc examples/common/link.ld VERBATIM)
    list(APPEND EXAMPLE_IMAGES "${EXAMPLE_IMAGE_${example}}")
  endforeach()
  configure_file(examples/measurement-feedback/mock.json
    "${CMAKE_CURRENT_BINARY_DIR}/examples/measurement-feedback/mock.json" COPYONLY)
  configure_file(examples/overlapping-pulses/profile.json
    "${CMAKE_CURRENT_BINARY_DIR}/examples/overlapping-pulses/profile.json" COPYONLY)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/examples/quma")
  add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/examples/quma/allxy.elf"
    COMMAND "${RISCV_AS}" -march=rv32i -mabi=ilp32 -I "${CMAKE_CURRENT_SOURCE_DIR}/examples/common"
      "${CMAKE_CURRENT_SOURCE_DIR}/examples/quma/allxy.S" -o "${CMAKE_CURRENT_BINARY_DIR}/examples/quma/allxy.o"
    COMMAND "${RISCV_LD}" -m elf32lriscv -T "${CMAKE_CURRENT_SOURCE_DIR}/examples/common/link.ld"
      "${CMAKE_CURRENT_BINARY_DIR}/examples/quma/allxy.o" -o "${CMAKE_CURRENT_BINARY_DIR}/examples/quma/allxy.elf"
    DEPENDS examples/quma/allxy.S examples/common/quantum.inc examples/common/link.ld VERBATIM)
  list(APPEND EXAMPLE_IMAGES "${CMAKE_CURRENT_BINARY_DIR}/examples/quma/allxy.elf")
  add_custom_target(example_images ALL DEPENDS ${EXAMPLE_IMAGES})
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(vliw_tests tests/vliw_tests.cpp)
  target_link_libraries(vliw_tests PRIVATE qsbit_core)
  add_test(NAME cpu.vliw COMMAND vliw_tests)
  set_tests_properties(cpu.vliw PROPERTIES TIMEOUT 15 LABELS "fast;unit")
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
  foreach(scenario normal reset)
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
  foreach(scenario bell feedback_one feedback_zero pulse)
    string(REGEX REPLACE "_.*" "" example "${scenario}")
    foreach(order normal reverse)
      add_test(NAME systemc.${scenario}.${order} COMMAND system_tests "${scenario}"
        "${EXAMPLE_IMAGE_${example}}" "${order}"
        "${CMAKE_CURRENT_BINARY_DIR}/${scenario}-${order}.jsonl")
      set_tests_properties(systemc.${scenario}.${order} PROPERTIES TIMEOUT 15 LABELS "fast;integration")
    endforeach()
  endforeach()
  if(QSBIT_PYTHON_BACKENDS)
    if(QSBIT_TEST_EXPERIMENTS)
      add_test(NAME python.repetition COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/repetition.py" --simulator "$<TARGET_FILE:qsbit_sim>"
        --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}"
        --assembler "${RISCV_AS}" --linker "${RISCV_LD}")
      set_tests_properties(python.repetition PROPERTIES TIMEOUT 120 LABELS "numerical;integration")
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
  if(QSBIT_TEST_AER OR QSBIT_TEST_PULSE)
    set(NUMERICAL_SCENARIOS "")
    set(CLI_SCENARIOS "")
    if(QSBIT_TEST_AER)
      list(APPEND NUMERICAL_SCENARIOS bell feedback capability)
      list(APPEND CLI_SCENARIOS bell feedback)
      add_test(NAME reference.cactus_golden COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/cactus_golden.py"
        --simulator "$<TARGET_FILE:qsbit_sim>"
        --assembler "${RISCV_AS}" --linker "${RISCV_LD}"
        --source "${CMAKE_CURRENT_SOURCE_DIR}" --build "${CMAKE_CURRENT_BINARY_DIR}")
      set_tests_properties(reference.cactus_golden PROPERTIES TIMEOUT 600
        LABELS "reference;integration;numerical")
    endif()
    if(QSBIT_TEST_PULSE)
      list(APPEND NUMERICAL_SCENARIOS pulse overlap joint)
      list(APPEND CLI_SCENARIOS pulse overlap)
    endif()
    add_test(NAME cli.examples COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_configs.py" --simulator "$<TARGET_FILE:qsbit_sim>"
      --build "${CMAKE_CURRENT_BINARY_DIR}"
      --scenarios ${CLI_SCENARIOS})
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
endif()
