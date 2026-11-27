#pragma once

#include <string>
#include <unordered_set>

namespace giga_edge {

struct FilterHotwordEventsNodeParams {
  std::unordered_set<std::string> hotwords;
};

} // namespace giga_edge