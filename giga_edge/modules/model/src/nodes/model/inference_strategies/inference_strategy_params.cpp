#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy_params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

#include <iostream>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(
  InferenceStrategyType, "sync", "skip_if_inferring", "wait_till_inferring", "wait_number_of_chunks", "queue"
);

namespace giga_edge {

template<>
AsyncStrategyParams ParseConfig(const YAML::Node& config) {
  AsyncStrategyParams result;

  result.thread_pool_name = config["thread_pool_name"].as<std::string>("");
  if (result.thread_pool_name.empty()) {
    throw std::runtime_error("'thread_pool_name' for async strategy is not specified");
  }
  result.thread_priority = config["thread_priority"].as<int>(0);

  return result;
}

template<>
WaitNumberOfChunksStrategyParams ParseConfig(const YAML::Node& config) {
  WaitNumberOfChunksStrategyParams result;

  auto chunk_cap = config["chunk_cap"].as<int>(1);
  if (chunk_cap < 1) {
    std::cerr << "[WARNING] 'chunk_cap' less than 1 is specified (" << chunk_cap << "). Min value 1 is forced instead."
              << std::endl;
    chunk_cap = 1;
  }
  result.chunk_cap = chunk_cap;

  return result;
}

template<>
QueueStrategyParams ParseConfig(const YAML::Node& config) {
  QueueStrategyParams result;

  result.max_queue = config["max_queue"].as<int>(1);
  if (result.max_queue < 1) {
    throw std::runtime_error("'max_queue' of queue strategy must be at least 1");
  }

  return result;
}

template<>
InferenceStrategyParams ParseConfig(const YAML::Node& config) {
  InferenceStrategyParams result;

  if (config["inference_strategy"]) {
    const auto& inference_strategy_config = config["inference_strategy"];
    result.type = EnumFromString<InferenceStrategyType>(inference_strategy_config["type"].as<std::string>("sync"));

    if (result.type != InferenceStrategyType::SYNC_INFERENCE) {
      result.async_strategy_params = ParseConfig<AsyncStrategyParams>(inference_strategy_config);
    }

    if (result.type == InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS) {
      result.wait_number_of_chunks_strategy_params = ParseConfig<WaitNumberOfChunksStrategyParams>(
        inference_strategy_config
      );
    }

    if (result.type == InferenceStrategyType::QUEUE) {
      result.queue_strategy_params = ParseConfig<QueueStrategyParams>(inference_strategy_config);
    }
  }

  return result;
}

} // namespace giga_edge