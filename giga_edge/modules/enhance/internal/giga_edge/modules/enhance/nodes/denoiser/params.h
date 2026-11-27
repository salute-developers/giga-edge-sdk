#pragma once
#include <stdint.h>
#include <string>

namespace giga_edge {
struct DenoiserParams {
  // frequency (Hz) of speech signal recording
  uint32_t sampling_freq = 16000;
  uint32_t frame_stride_ms = 16;
  std::string config_yaml;
};
} // namespace giga_edge
