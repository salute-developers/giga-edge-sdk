#pragma once

#include <cstdint>
#include <vector>

namespace giga_edge {

struct BatchFilterNodeParams {
  std::vector<int32_t> ids_to_take;
};

} // namespace giga_edge