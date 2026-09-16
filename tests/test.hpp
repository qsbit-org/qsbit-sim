#pragma once

#include "qsbit/error.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(expression)                                                                          \
  do {                                                                                             \
    if (!(expression))                                                                             \
      throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) +            \
                               ": " #expression);                                                  \
  } while (false)

template <typename F> void faults(qsbit::ErrorCode expected, F &&run) {
  try {
    run();
  } catch (const qsbit::Fault &fault) {
    if (fault.code() != expected)
      throw std::runtime_error(std::string("expected ") + qsbit::name(expected) + ", got " +
                               qsbit::name(fault.code()));
    return;
  }
  throw std::runtime_error(std::string("expected fault ") + qsbit::name(expected));
}
