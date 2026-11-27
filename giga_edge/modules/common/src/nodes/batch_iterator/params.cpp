#include "giga_edge/modules/common/nodes/batch_iterator/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
BatchIteratorNodeParams ParseConfig(const YAML::Node& config) {
  BatchIteratorNodeParams result;
  result.batch_size = config["batch_size"].as<int32_t>();
  return result;
}

} // namespace giga_edge