#include "giga_edge/modules/common/nodes/has_substring_by_key/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
HasSubstringByKeyNodeParams ParseConfig(const YAML::Node& config) {
  HasSubstringByKeyNodeParams result;
  for (const auto& substring_files : config["substring_files"]) {
    result.substring_files.emplace(substring_files.first.as<std::string>(), substring_files.second.as<std::string>());
  }
  return result;
}

} // namespace giga_edge