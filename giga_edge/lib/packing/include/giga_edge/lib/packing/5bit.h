#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace giga_edge::packing {

constexpr const char* const unpack_5bit_custom_op_name = "Unpack5Bit";

std::vector<std::uint8_t> Pack5BitData(const std::uint8_t* data, std::size_t size);
bool Unpack5BitData(const std::uint8_t* src, std::size_t src_size, std::uint8_t* dst, std::size_t dst_size);

} // namespace giga_edge::packing
