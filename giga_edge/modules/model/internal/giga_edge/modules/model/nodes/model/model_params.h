#pragma once

#include "giga_edge/modules/model/models/model_params.h"
#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy_params.h"

#include <string>

namespace giga_edge {

struct ModelNodeParams : ModelParams {
  std::string path;
  InferenceStrategyParams inference_strategy;
};

} // namespace giga_edge