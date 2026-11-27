#pragma once

#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy_params.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace giga_edge {

enum class InferenceStrategyAction {
  FINISH_PROCESSING = 0,
  READ_INPUTS,
  SYNC_INFERENCE,
  ASYNC_INFERENCE,
  PUSH_OUTPUTS,
  PUSH_EMPTY_OUTPUTS,
  CHECK_INFERENCE,
  WAIT_INFERENCE,
  ENQUEUE_INFERENCE,
  WAIT_QUEUE,
  PUSH_QUEUED_OUTPUTS,
};

enum class InferenceState {
  IDLE,
  INFERRING,
  INFERRED,
};

class IInferenceStrategy {
public:
  virtual ~IInferenceStrategy() = default;

  virtual InferenceStrategyAction NextAction() = 0;

  virtual void OnInputs() = 0;
  virtual void OnEmptyInputs() = 0;
  virtual void OnForceWait() {}
  virtual void OnReset() = 0;
  virtual void OnInferred() = 0;
  virtual void OnPushedOutputs() = 0;

protected:
  virtual void OnProcessFinished() = 0;

  enum class InputStatus {
    NEED_TO_READ,
    EMPTY,
    NOT_EMPTY,
  };
};

using InferenceStrategyPtr = std::unique_ptr<IInferenceStrategy>;

namespace inference_strategy_factory_impl {

InferenceStrategyPtr CreateSyncInferenceStrategy(const InferenceStrategyParams& inference_strategy);
InferenceStrategyPtr CreateSkipIfInferringStrategy(const InferenceStrategyParams& inference_strategy);
InferenceStrategyPtr CreateWaitTillInferringStrategy(const InferenceStrategyParams& inference_strategy);
InferenceStrategyPtr CreateWaitNumberOfChunksStrategy(const InferenceStrategyParams& inference_strategy);
InferenceStrategyPtr CreateQueueStrategy(const InferenceStrategyParams& inference_strategy);

using FuncT = std::function<InferenceStrategyPtr(const InferenceStrategyParams&)>;
static std::unordered_map<InferenceStrategyType, FuncT> create_strategy_by_type = {
  {InferenceStrategyType::SYNC_INFERENCE, inference_strategy_factory_impl::CreateSyncInferenceStrategy},
  {InferenceStrategyType::SKIP_IF_INFERRING, inference_strategy_factory_impl::CreateSkipIfInferringStrategy},
  {InferenceStrategyType::WAIT_TILL_INFERRING, inference_strategy_factory_impl::CreateWaitTillInferringStrategy},
  {InferenceStrategyType::WAIT_NUMBER_OF_CHUNKS, inference_strategy_factory_impl::CreateWaitNumberOfChunksStrategy},
  {InferenceStrategyType::QUEUE, inference_strategy_factory_impl::CreateQueueStrategy},
};

} // namespace inference_strategy_factory_impl

class InferenceStrategyFactory {
public:
  static InferenceStrategyPtr CreateInferenceStrategy(const InferenceStrategyParams& inference_strategy);
};

} // namespace giga_edge