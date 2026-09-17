option(QSBIT_FETCH_SYSTEMC "Fetch pinned SystemC if no installation is found" ON)
find_package(SystemCLanguage 3.0.1.20241015 QUIET CONFIG)
if(SystemCLanguage_FOUND)
  if(NOT SystemC_CXX_STANDARD EQUAL 20)
    message(FATAL_ERROR "The installed SystemC must be built with C++20")
  endif()
elseif(QSBIT_FETCH_SYSTEMC)
  include(FetchContent)
  set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(ENABLE_REGRESSION OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(systemc
    URL https://codeload.github.com/accellera-official/systemc/tar.gz/11ad094d282fd5330b27ab57f90f9d231a763da1
    URL_HASH SHA256=7fb54a11e44914bcecf6d8ce40199811c912683f22c798fcc00b1deb23abb4fa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(systemc)
  get_target_property(systemc_includes systemc INTERFACE_INCLUDE_DIRECTORIES)
  set_property(TARGET systemc APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    "${systemc_includes}")
else()
  message(FATAL_ERROR
    "SystemC was not found. Set CMAKE_PREFIX_PATH to a C++20 SystemC installation, "
    "or enable QSBIT_FETCH_SYSTEMC.")
endif()
