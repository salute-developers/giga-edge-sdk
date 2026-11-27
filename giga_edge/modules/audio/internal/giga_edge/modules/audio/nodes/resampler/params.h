#pragma once

#include <cstdint>

namespace giga_edge {
struct ResamplerParams {
  uint32_t from_freq = 16000; // freq in Hz
  uint32_t to_freq = 16000;
};
} // namespace giga_edge