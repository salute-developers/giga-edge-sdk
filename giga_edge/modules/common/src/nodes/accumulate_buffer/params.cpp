#include "giga_edge/modules/common/nodes/accumulate_buffer/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(AccumulateBufferNodeParams::Operation, "inputs", "drop", "push");

namespace giga_edge {

template<>
AccumulateBufferNodeParams ParseConfig(const YAML::Node& config) {
  AccumulateBufferNodeParams result;
  result.max_size = config["max_size"].as<long long>(result.max_size);
  result.clear_after_push = config["clear_after_push"].as<bool>(result.clear_after_push);
  if (config["operations_priority"]) {
    result.operations_priority.clear();
    for (const auto& operation : config["operations_priority"]) {
      result.operations_priority.push_back(
        EnumFromString<AccumulateBufferNodeParams::Operation>(operation.as<std::string>())
      );
    }
  }
  return result;
}

} // namespace giga_edge