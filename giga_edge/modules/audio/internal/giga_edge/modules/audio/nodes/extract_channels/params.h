#pragma once

#include <cstdint>
#include <vector>

namespace giga_edge {

struct ExtractChannelsNodeParams {
  std::vector<int32_t> channels;
};

} // namespace giga_edge