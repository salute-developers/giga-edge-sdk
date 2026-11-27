#pragma once

#include <string>
#include <unordered_map>

namespace giga_edge {

struct HasSubstringByKeyNodeParams {
  std::unordered_map<std::string, std::string> substring_files;
};

} // namespace giga_edge