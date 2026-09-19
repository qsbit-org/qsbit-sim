if(DEFINED CACHE{SystemCLanguage_DIR} AND
    NOT EXISTS "${SystemCLanguage_DIR}/SystemCLanguageTargets.cmake")
  unset(SystemCLanguage_DIR CACHE)
endif()

find_package(SystemCLanguage 3.0.1.20241015 EXACT QUIET CONFIG
  NO_CMAKE_PACKAGE_REGISTRY)
if(NOT SystemCLanguage_FOUND)
  message(FATAL_ERROR
    "SystemC 3.0.1 (C++20) was not found. Install it as described in "
    "docs/prerequisites.md, then set CMAKE_PREFIX_PATH to its install prefix.")
endif()

if(NOT SystemC_CXX_STANDARD EQUAL 20)
  message(FATAL_ERROR "The installed SystemC must be built with C++20")
endif()
