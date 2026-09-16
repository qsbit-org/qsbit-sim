#include "qsbit/image.hpp"
#include <algorithm>
#include <limits>

namespace qsbit {
namespace {
std::uint32_t field(std::span<const std::uint8_t> b, std::size_t p, unsigned width) {
  require(p <= b.size() && width <= b.size() - p, ErrorCode::InvalidImage, "truncated ELF field");
  std::uint32_t value = 0;
  for (unsigned i = 0; i < width; ++i)
    value |= std::uint32_t{b[p + i]} << (8 * i);
  return value;
}
} // namespace
ProgramImage::ProgramImage(std::uint32_t base, std::uint32_t size) : base_(base) {
  require(size > 0 && std::uint64_t{base} + size <= (std::uint64_t{1} << 32),
          ErrorCode::InvalidImage, "memory map exceeds RV32 address space");
  bytes_.resize(size);
}
ProgramImage ProgramImage::raw(std::span<const std::uint8_t> file, std::uint32_t address,
                               std::uint32_t base, std::uint32_t size) {
  require(!file.empty() && file.size() % 4 == 0 && address % 4 == 0, ErrorCode::InvalidImage,
          "raw image must contain aligned RV32 words");
  require(address >= base && std::uint64_t{address - base} + file.size() <= size,
          ErrorCode::InvalidImage, "raw image is outside memory");
  ProgramImage image(base, size);
  image.entry_ = address;
  std::copy(file.begin(), file.end(), image.bytes_.begin() + (address - base));
  image.segments_.push_back({address, static_cast<std::uint32_t>(file.size()), true, true, true});
  return image;
}
ProgramImage ProgramImage::elf(std::span<const std::uint8_t> file, std::uint32_t base,
                               std::uint32_t size) {
  require(file.size() >= 52 && field(file, 0, 4) == 0x464c457f && file[4] == 1 && file[5] == 1 &&
              file[6] == 1,
          ErrorCode::InvalidImage, "expected little-endian ELF32");
  require(field(file, 16, 2) == 2 && field(file, 18, 2) == 243 && field(file, 20, 4) == 1 &&
              field(file, 40, 2) == 52 && field(file, 36, 4) == 0,
          ErrorCode::InvalidImage, "expected an uncompressed RV32I executable");
  const auto phoff = field(file, 28, 4), count = field(file, 44, 2);
  require(field(file, 42, 2) == 32 && count > 0 &&
              std::uint64_t{phoff} + std::uint64_t{count} * 32 <= file.size(),
          ErrorCode::InvalidImage, "invalid ELF program header table");
  ProgramImage image(base, size);
  image.entry_ = field(file, 24, 4);
  for (std::uint32_t n = 0; n < count; ++n) {
    const std::size_t p = std::size_t{phoff} + std::size_t{n} * 32;
    if (field(file, p, 4) != 1)
      continue;
    const auto off = field(file, p + 4, 4), addr = field(file, p + 8, 4);
    const auto filesz = field(file, p + 16, 4), memsz = field(file, p + 20, 4);
    const auto flags = field(file, p + 24, 4), align = field(file, p + 28, 4);
    require(filesz <= memsz && std::uint64_t{off} + filesz <= file.size() && addr >= base &&
                std::uint64_t{addr - base} + memsz <= size,
            ErrorCode::InvalidImage, "ELF segment is out of bounds");
    require(align <= 1 || ((align & (align - 1)) == 0 && addr % align == off % align),
            ErrorCode::InvalidImage, "invalid ELF segment alignment");
    if (memsz == 0)
      continue;
    for (const auto &other : image.segments_)
      require(std::uint64_t{addr} + memsz <= other.address ||
                  std::uint64_t{other.address} + other.size <= addr,
              ErrorCode::InvalidImage, "overlapping ELF segments");
    image.segments_.push_back({addr, memsz, (flags & 4) != 0, (flags & 2) != 0, (flags & 1) != 0});
    std::copy_n(file.begin() + off, filesz, image.bytes_.begin() + (addr - base));
  }
  require(image.entry_ % 4 == 0 && std::any_of(image.segments_.begin(), image.segments_.end(),
                                               [&](const Segment &s) {
                                                 return s.executable && image.entry_ >= s.address &&
                                                        std::uint64_t{image.entry_} + 4 <=
                                                            std::uint64_t{s.address} + s.size;
                                               }),
          ErrorCode::InvalidImage, "entry is not an aligned executable word");
  return image;
}
std::size_t ProgramImage::offset(std::uint32_t address, std::uint8_t width, bool store,
                                 bool instruction) const {
  const auto access = instruction ? ErrorCode::InstructionAccess
                      : store     ? ErrorCode::StoreAccess
                                  : ErrorCode::LoadAccess;
  const auto aligned = instruction ? ErrorCode::InstructionMisaligned
                       : store     ? ErrorCode::StoreMisaligned
                                   : ErrorCode::LoadMisaligned;
  require(width == 1 || width == 2 || width == 4, access, "invalid memory access width");
  require(address % width == 0, aligned, "misaligned memory access");
  require(address >= base_ && std::uint64_t{address - base_} + width <= bytes_.size(), access,
          "memory access outside configured RAM");
  bool executable = false;
  for (const auto &s : segments_) {
    if (std::uint64_t{address} < std::uint64_t{s.address} + s.size &&
        std::uint64_t{s.address} < std::uint64_t{address} + width) {
      require(address >= s.address &&
                  std::uint64_t{address} + width <= std::uint64_t{s.address} + s.size,
              access, "memory access straddles segment boundary");
      require(instruction ? s.executable
              : store     ? s.writable
                          : s.readable,
              access, "segment permission violation");
      executable = s.executable;
    }
  }
  require(!instruction || executable, access, "instruction fetch outside executable segments");
  return address - base_;
}
std::uint32_t ProgramImage::read(std::uint32_t address, std::uint8_t width,
                                 bool instruction) const {
  const auto p = offset(address, width, false, instruction);
  std::uint32_t value = 0;
  for (unsigned n = 0; n < width; ++n)
    value |= std::uint32_t{bytes_[p + n]} << (n * 8);
  return value;
}
void ProgramImage::write(std::uint32_t address, std::uint8_t width, std::uint32_t value) {
  const auto p = offset(address, width, true, false);
  for (unsigned n = 0; n < width; ++n)
    bytes_[p + n] = static_cast<std::uint8_t>(value >> (n * 8));
}
} // namespace qsbit
