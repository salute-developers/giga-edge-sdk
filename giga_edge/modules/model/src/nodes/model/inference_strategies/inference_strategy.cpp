#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"

#include "giga_edge/lib/enum/enum.h"

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(
  InferenceStrategyAction,
  "FINISH_PROCESSING",
  "READ_INPUTS",
  "SYNC_INFERENCE",
  "ASYNC_INFERENCE",
  "PUSH_OUTPUTS",
  "PUSH_EMPTY_OUTPUTS",
  "CHECK_INFERENCE",
  "WAIT_INFERENCE",
);