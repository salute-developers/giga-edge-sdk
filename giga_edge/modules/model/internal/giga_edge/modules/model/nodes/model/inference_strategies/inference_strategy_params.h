#pragma once

#include <string>

namespace giga_edge {

enum class InferenceStrategyType {
  SYNC_INFERENCE = 0,
  SKIP_IF_INFERRING,
  WAIT_TILL_INFERRING,
  WAIT_NUMBER_OF_CHUNKS,
  QUEUE,
};

struct AsyncStrategyParams {
  std::string thread_pool_name;
  int thread_priority = 0;
};

struct WaitNumberOfChunksStrategyParams {
  int chunk_cap = 0;
};

struct QueueStrategyParams {
  int max_queue = 1;
};

struct InferenceStrategyParams {
  InferenceStrategyType type = InferenceStrategyType::SYNC_INFERENCE;

  AsyncStrategyParams async_strategy_params;
  WaitNumberOfChunksStrategyParams wait_number_of_chunks_strategy_params;
  QueueStrategyParams queue_strategy_params;
};

} // namespace giga_edge