cmake_minimum_required(VERSION 3.24)
project(QsbitContracts VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(qsbit_contracts INTERFACE)
add_library(qsbit::contracts ALIAS qsbit_contracts)
set_target_properties(qsbit_contracts PROPERTIES EXPORT_NAME contracts)
target_compile_features(qsbit_contracts INTERFACE cxx_std_20)
target_include_directories(qsbit_contracts INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
install(TARGETS qsbit_contracts EXPORT QsbitContractsTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT QsbitContractsTargets NAMESPACE qsbit::
  FILE QsbitContractsConfig.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QsbitContracts)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QsbitContractsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QsbitContractsConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/QsbitContracts)
