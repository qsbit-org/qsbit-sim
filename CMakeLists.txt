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
  src/backend.cpp src/device.cpp src/defaults.cpp src/cpu/vliw.cpp src/core.cpp src/sync.cpp)
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
add_executable(qsbit_sim src/main.cpp src/app/config.cpp src/app/output.cpp)
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
  include(cmake/Tests.cmake)
endif()
