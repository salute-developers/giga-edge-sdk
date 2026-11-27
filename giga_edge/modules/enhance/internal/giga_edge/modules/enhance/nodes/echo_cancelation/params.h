#pragma once
#include <stdint.h>
#include <string>

namespace giga_edge {
struct EchoCancelationParams {
  std::string config_yaml;
  uint32_t frame_len;
};
} // namespace giga_edge
