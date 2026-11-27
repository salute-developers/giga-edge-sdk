#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"

#include <catch.hpp>

#include <iostream>
#include <sstream>

using namespace giga_edge;

TEST_CASE("Model node inference strategies, sync flow without input data", "[runtime][nodes]") {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::SYNC_INFERENCE,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE("Model node inference strategies, sync flow with input data", "[runtime][nodes]") {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::SYNC_INFERENCE,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::SYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE("Model node inference strategies,  skip if inferring flow without input data", "[runtime][nodes]") {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::SKIP_IF_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, skip if inferring flow with input data and without input on passing check",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::SKIP_IF_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::CHECK_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::CHECK_INFERENCE);
  strategy->OnInferred();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, skip if inferring flow with input data and with input on passing check",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::SKIP_IF_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::CHECK_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::CHECK_INFERENCE);
  strategy->OnInferred();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);
  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE("Model node inference strategies, wait till inferring flow without input data", "[runtime][nodes]") {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_TILL_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, wait till inferring flow with input data and inference ending without input",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_TILL_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::CHECK_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::CHECK_INFERENCE);
  strategy->OnInferred();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, wait till inferring flow with input data and inference ending with input",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_TILL_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::WAIT_INFERENCE);
  strategy->OnInferred();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);
  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, wait till inferring: force_wait on an empty input drains a busy tail",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_TILL_INFERRING,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::ASYNC_INFERENCE);
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);
  strategy->OnPushedOutputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::FINISH_PROCESSING);

  // With the node busy, the tail goes to the pool.
  strategy->OnForceWait();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::WAIT_INFERENCE);
  strategy->OnInferred();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_OUTPUTS);
  strategy->OnPushedOutputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::ASYNC_INFERENCE);
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::FINISH_PROCESSING);

  // An empty element with force_wait drains the tail.
  strategy->OnForceWait();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnEmptyInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::WAIT_INFERENCE);
  strategy->OnInferred();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_OUTPUTS);
  strategy->OnPushedOutputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::FINISH_PROCESSING);

  // The node is free: the next empty element waits for nothing.
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnEmptyInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);
}

TEST_CASE("Model node inference strategies, wait number of chunks flow without input data", "[runtime][nodes]") {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS,
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 2,
    }
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, wait number of chunks flow with input data and inference ending without input",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS,
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 2,
    }
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnEmptyInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::WAIT_INFERENCE);
  strategy->OnInferred();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);
  strategy->OnPushedOutputs();

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

TEST_CASE(
  "Model node inference strategies, wait number of chunks flow with input data and inference ending with input",
  "[runtime][nodes]"
) {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS,
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 2,
    }
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  auto action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_EMPTY_OUTPUTS);

  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::READ_INPUTS);

  strategy->OnInputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::WAIT_INFERENCE);
  strategy->OnInferred();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::PUSH_OUTPUTS);
  strategy->OnPushedOutputs();
  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::ASYNC_INFERENCE);

  action = strategy->NextAction();
  REQUIRE(action == InferenceStrategyAction::FINISH_PROCESSING);
}

namespace giga_edge {

inline bool operator==(const AsyncStrategyParams& lhs, const AsyncStrategyParams& rhs) {
  return lhs.thread_pool_name == rhs.thread_pool_name;
}

inline bool operator==(const WaitNumberOfChunksStrategyParams& lhs, const WaitNumberOfChunksStrategyParams& rhs) {
  return lhs.chunk_cap == rhs.chunk_cap;
}

inline bool operator==(const InferenceStrategyParams& lhs, const InferenceStrategyParams& rhs) {
  return lhs.type == rhs.type && lhs.async_strategy_params == rhs.async_strategy_params
    && lhs.wait_number_of_chunks_strategy_params == rhs.wait_number_of_chunks_strategy_params;
}

} // namespace giga_edge

TEST_CASE("Model node inference strategies, test parse config", "[runtime][nodes]") {
  REQUIRE(
    ParseConfig<InferenceStrategyParams>(YAML::Load(""))
    == InferenceStrategyParams{
      .type = InferenceStrategyType::SYNC_INFERENCE,
      .wait_number_of_chunks_strategy_params = {
        .chunk_cap = 0,
      },
    }
  );
  REQUIRE(ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: sync\n"
  )) == InferenceStrategyParams{
    .type = InferenceStrategyType::SYNC_INFERENCE,
    .async_strategy_params = {
      .thread_pool_name = "",
    },
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 0,
    },
  });

  REQUIRE_THROWS_WITH(
    ParseConfig<InferenceStrategyParams>(YAML::Load(
      "inference_strategy:\n"
      "  type: skip_if_inferring\n"
    )),
    "'thread_pool_name' for async strategy is not specified"
  );

  REQUIRE_THROWS_WITH(
    ParseConfig<InferenceStrategyParams>(YAML::Load(
      "inference_strategy:\n"
      "  type: wait_till_inferring\n"
    )),
    "'thread_pool_name' for async strategy is not specified"
  );

  REQUIRE_THROWS_WITH(
    ParseConfig<InferenceStrategyParams>(YAML::Load(
      "inference_strategy:\n"
      "  type: wait_number_of_chunks\n"
    )),
    "'thread_pool_name' for async strategy is not specified"
  );

  REQUIRE(ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: skip_if_inferring\n"
    "  thread_pool_name: some_thread_pool_id\n"
    "  chunk_cap: 1\n"
  )) == InferenceStrategyParams{
    .type = InferenceStrategyType::SKIP_IF_INFERRING,
    .async_strategy_params = {
      .thread_pool_name = "some_thread_pool_id",
    },
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 0,
    },
  });

  REQUIRE(ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: wait_till_inferring\n"
    "  thread_pool_name: some_thread_pool_id\n"
    "  chunk_cap: -1\n"
  )) == InferenceStrategyParams{
    .type = InferenceStrategyType::WAIT_TILL_INFERRING,
    .async_strategy_params = {
      .thread_pool_name = "some_thread_pool_id",
    },
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 0,
    },
  });

  REQUIRE(ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: wait_number_of_chunks\n"
    "  thread_pool_name: some_thread_pool_id\n"
  )) == InferenceStrategyParams{
    .type = InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS,
    .async_strategy_params = {
      .thread_pool_name = "some_thread_pool_id",
    },
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 1,
    },
  });
  REQUIRE(ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: wait_number_of_chunks\n"
    "  thread_pool_name: some_thread_pool_id\n"
    "  chunk_cap: 0\n"
  )) == InferenceStrategyParams{
    .type = InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS,
    .async_strategy_params = {
      .thread_pool_name = "some_thread_pool_id",
    },
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 1,
    },
  });
  REQUIRE(ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: wait_number_of_chunks\n"
    "  thread_pool_name: some_thread_pool_id\n"
    "  chunk_cap: 10\n"
  )) == InferenceStrategyParams{
    .type = InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS,
    .async_strategy_params = {
      .thread_pool_name = "some_thread_pool_id",
    },
    .wait_number_of_chunks_strategy_params = {
      .chunk_cap = 10,
    },
  });
}

TEST_CASE("Model node inference strategies, queue params", "[runtime][nodes]") {
  const auto parse = [](const std::string& max_queue) {
    return ParseConfig<InferenceStrategyParams>(YAML::Load(
      "inference_strategy:\n"
      "  type: queue\n"
      "  thread_pool_name: some_thread_pool_id\n"
      + max_queue
    ));
  };
  const auto defaults = parse("");
  REQUIRE(defaults.type == InferenceStrategyType::QUEUE);
  REQUIRE(defaults.async_strategy_params.thread_pool_name == "some_thread_pool_id");
  REQUIRE(defaults.queue_strategy_params.max_queue == 1);
  REQUIRE(parse("  max_queue: 1\n").queue_strategy_params.max_queue == 1);
  REQUIRE(parse("  max_queue: 3\n").queue_strategy_params.max_queue == 3);
  for (const auto* wrong : {"  max_queue: 0\n", "  max_queue: -1\n"}) {
    CAPTURE(wrong);
    REQUIRE_THROWS_WITH(parse(wrong), Catch::Contains("'max_queue' of queue strategy must be at least 1"));
  }
}

TEST_CASE("Model node inference strategies, queue flow", "[runtime][nodes]") {
  const InferenceStrategyParams params = {
    .type = InferenceStrategyType::QUEUE,
  };
  auto strategy = InferenceStrategyFactory::CreateInferenceStrategy(params);

  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::ENQUEUE_INFERENCE);
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_QUEUED_OUTPUTS);
  strategy->OnPushedOutputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::FINISH_PROCESSING);

  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnEmptyInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_QUEUED_OUTPUTS);
  strategy->OnPushedOutputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::FINISH_PROCESSING);

  strategy->OnForceWait();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::ENQUEUE_INFERENCE);
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::WAIT_QUEUE);
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_QUEUED_OUTPUTS);
  strategy->OnPushedOutputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::FINISH_PROCESSING);

  REQUIRE(strategy->NextAction() == InferenceStrategyAction::READ_INPUTS);
  strategy->OnEmptyInputs();
  REQUIRE(strategy->NextAction() == InferenceStrategyAction::PUSH_QUEUED_OUTPUTS);
}

TEST_CASE("Model node inference strategies, the warning names the given chunk_cap", "[runtime][nodes]") {
  std::ostringstream log;
  auto* const stderr_buffer = std::cerr.rdbuf(log.rdbuf());
  const auto params = ParseConfig<InferenceStrategyParams>(YAML::Load(
    "inference_strategy:\n"
    "  type: wait_number_of_chunks\n"
    "  thread_pool_name: some_thread_pool_id\n"
    "  chunk_cap: -3\n"
  ));
  std::cerr.rdbuf(stderr_buffer);
  REQUIRE(params.wait_number_of_chunks_strategy_params.chunk_cap == 1);
  REQUIRE_THAT(log.str(), Catch::Contains("(-3)"));
}
