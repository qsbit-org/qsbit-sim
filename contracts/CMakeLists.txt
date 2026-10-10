cmake_minimum_required(VERSION 3.24)
project(QsbitContracts VERSION 0.2.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema/executable.json" executable_abi)
foreach(key schema isa word_bytes)
  string(JSON ${key} GET "${executable_abi}" ${key})
endforeach()
foreach(mode static adaptive)
  foreach(key identifier memory_size output_count output_data output_capacity data_start stack_bottom stack_pointer)
    string(JSON ${mode}_${key} GET "${executable_abi}" ${mode} ${key})
  endforeach()
endforeach()
configure_file(executable.hpp.in generated/qsbit/contracts/executable.hpp @ONLY)
if(NOT PROJECT_IS_TOP_LEVEL)
  set(QsbitContracts_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schema" PARENT_SCOPE)
  set(QsbitContracts_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)
endif()
add_library(qsbit_contracts INTERFACE)
add_library(qsbit::contracts ALIAS qsbit_contracts)
set_target_properties(qsbit_contracts PROPERTIES EXPORT_NAME contracts)
target_compile_features(qsbit_contracts INTERFACE cxx_std_20)
target_include_directories(qsbit_contracts INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS qsbit_contracts EXPORT QsbitContractsTargets)
install(DIRECTORY include/ "${CMAKE_CURRENT_BINARY_DIR}/generated/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES check_dependencies.py DESTINATION ${CMAKE_INSTALL_DATADIR}/qsbit-contracts)
install(DIRECTORY schema/ DESTINATION ${CMAKE_INSTALL_DATADIR}/qsbit-contracts)
install(EXPORT QsbitContractsTargets NAMESPACE qsbit::
  FILE QsbitContractsTargets.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QsbitContracts)
configure_package_config_file(Config.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/QsbitContractsConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QsbitContracts)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QsbitContractsConfig.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QsbitContracts)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QsbitContractsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QsbitContractsConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QsbitContracts)
