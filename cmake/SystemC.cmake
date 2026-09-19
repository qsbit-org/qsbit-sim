if(NOT CMAKE_BUILD_TYPE STREQUAL QSBIT_CONAN_BUILD_TYPE)
  message(FATAL_ERROR "Build type differs from prepared Conan dependencies. Use the matching preset.")
endif()
get_filename_component(QSBIT_CONAN_GENERATORS "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
unset(SystemCLanguage_DIR CACHE)
unset(SystemCLanguage_DIR)
find_package(SystemCLanguage REQUIRED CONFIG
  PATHS "${QSBIT_CONAN_GENERATORS}" NO_DEFAULT_PATH)
