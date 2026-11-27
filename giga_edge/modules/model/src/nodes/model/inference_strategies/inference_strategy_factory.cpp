#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"
#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy_params.h"

#include <stdexcept>

namespace giga_edge {

InferenceStrategyPtr InferenceStrategyFactory::CreateInferenceStrategy(
  const InferenceStrategyParams& inference_strategy
) {
  const InferenceStrategyType& type = inference_strategy.type;
  if (
    inference_strategy_factory_impl::create_strategy_by_type.find(type)
    == inference_strategy_factory_impl::create_strategy_by_type.end()
  ) {
    throw std::runtime_error("Unsupported inference strategy type is defined: " + std::string(EnumToString(type)));
    return nullptr;
  }
  return inference_strategy_factory_impl::create_strategy_by_type[type](inference_strategy);
}

} // namespace giga_edge