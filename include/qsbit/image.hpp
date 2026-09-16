#pragma once

#include "qsbit/error.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace qsbit {
struct Segment {
  std::uint32_t address = 0, size = 0;
  bool readable = false, writable = false, executable = false;
};
class ProgramImage {
public:
  ProgramImage(std::uint32_t base, std::uint32_t size);
  static ProgramImage elf(std::span<const std::uint8_t> file, std::uint32_t base,
                          std::uint32_t size);
  static ProgramImage raw(std::span<const std::uint8_t> file, std::uint32_t address,
                          std::uint32_t base, std::uint32_t size);
  [[nodiscard]] std::uint32_t entry() const { return entry_; }
  [[nodiscard]] std::uint32_t base() const { return base_; }
  [[nodiscard]] std::span<const std::uint8_t> bytes() const { return bytes_; }
  [[nodiscard]] const std::vector<Segment> &segments() const { return segments_; }
  [[nodiscard]] std::uint32_t read(std::uint32_t address, std::uint8_t width,
                                   bool instruction = false) const;
  void write(std::uint32_t address, std::uint8_t width, std::uint32_t value);

private:
  [[nodiscard]] std::size_t offset(std::uint32_t address, std::uint8_t width, bool store,
                                   bool instruction) const;
  std::uint32_t base_, entry_ = 0;
  std::vector<std::uint8_t> bytes_;
  std::vector<Segment> segments_;
};
} // namespace qsbit
