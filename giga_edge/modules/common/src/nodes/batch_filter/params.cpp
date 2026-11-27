#include "giga_edge/modules/common/nodes/batch_filter/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
BatchFilterNodeParams ParseConfig(const YAML::Node& config) {
  BatchFilterNodeParams result;
  for (auto id : config["ids_to_take"]) {
    result.ids_to_take.push_back(id.as<int32_t>());
  }
  return result;
}

} // namespace giga_edge