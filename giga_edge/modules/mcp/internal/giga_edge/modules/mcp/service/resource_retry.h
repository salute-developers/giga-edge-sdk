#pragma once

#include <algorithm>
#include <cstdint>

namespace giga_edge {
namespace mcp {
namespace detail {

inline uint32_t NextResourceRetryDelay(uint32_t previous, uint32_t initial, uint32_t maximum) {
  if (initial == 0) {
    return 0;
  }
  return previous == 0 ? initial
                       : static_cast<uint32_t>(std::min<uint64_t>(uint64_t{previous} * 2, std::max(initial, maximum)));
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
