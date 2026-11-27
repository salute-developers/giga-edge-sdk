#pragma once

#include <cstdint>
#include <vector>

namespace giga_edge {

struct InterleaveNodeParams {
  std::vector<int32_t> channels;
};

} // namespace giga_edge