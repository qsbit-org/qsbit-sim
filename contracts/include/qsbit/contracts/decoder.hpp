#pragma once
#include <cstdint>
namespace qsbit::contract::decoder {
inline constexpr std::uint32_t RegisterBytes = 32;
inline constexpr std::uint32_t Select = 0, Count = 4, DataLow = 8, DataHigh = 12, Tag = 16,
                               Command = 20, ResultLow = 24, ResultHigh = 28;
inline constexpr std::uint32_t Submit = 1, Reset = 2, Consume = 3;
inline constexpr std::uint32_t Ready = 1, Busy = 2;
} // namespace qsbit::contract::decoder
