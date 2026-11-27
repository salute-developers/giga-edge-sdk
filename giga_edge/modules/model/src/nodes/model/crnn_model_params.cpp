#include "giga_edge/modules/model/nodes/model/crnn_model_params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
CRNNModelNodeParams ParseConfig(const YAML::Node& config) {
  CRNNModelNodeParams result;

  result.type = ParseModelType(config["type"].as<std::string>());
  result.path = config["path"].as<std::string>();

  switch (result.type) {
    case ModelType::WINDOW_CRNN:
      result.window_crnn_model_params = ParseConfig<WindowCRNNModelParams>(config);
      break;
    case ModelType::STATEFUL_CRNN:
      result.stateful_crnn_model_params = ParseConfig<CRNNModelParams>(config);
      break;
  }
  return result;
}

} // namespace giga_edge