#include "giga_edge/modules/model/nodes/model/model_params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

namespace giga_edge {

template<>
ModelNodeParams ParseConfig(const YAML::Node& config) {
  ModelNodeParams result = {ParseConfig<ModelParams>(config)};
  result.path = config["path"].as<std::string>();
  result.inference_strategy = ParseConfig<InferenceStrategyParams>(config);

  return result;
}

} // namespace giga_edge