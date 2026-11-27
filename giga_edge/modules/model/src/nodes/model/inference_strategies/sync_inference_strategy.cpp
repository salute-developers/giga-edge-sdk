#include "giga_edge/modules/model/nodes/model/inference_strategies/sync_inference_strategy.h"

#include <stdexcept>

namespace giga_edge {

namespace inference_strategy_factory_impl {
std::unique_ptr<IInferenceStrategy> CreateSyncInferenceStrategy(const InferenceStrategyParams& inference_strategy) {
  return std::make_unique<SyncInferenceStrategy>();
}
} // namespace inference_strategy_factory_impl

InferenceStrategyAction SyncInferenceStrategy::NextAction() {
  if (is_finished_) {
    OnProcessFinished();
    return InferenceStrategyAction::FINISH_PROCESSING;
  }

  if (input_status_ == InputStatus::NEED_TO_READ) {
    return InferenceStrategyAction::READ_INPUTS;
  }

  if (input_status_ == InputStatus::NOT_EMPTY) {
    if (is_inference_active_) {
      return InferenceStrategyAction::PUSH_OUTPUTS;
    }
    is_inference_active_ = true;
    return InferenceStrategyAction::SYNC_INFERENCE;
  }

  return InferenceStrategyAction::PUSH_EMPTY_OUTPUTS;
}

void SyncInferenceStrategy::OnReset() {
  OnProcessFinished();
}

void SyncInferenceStrategy::OnInputs() {
  input_status_ = InputStatus::NOT_EMPTY;
}

void SyncInferenceStrategy::OnEmptyInputs() {
  input_status_ = InputStatus::EMPTY;
}

void SyncInferenceStrategy::OnInferred() {
  throw std::runtime_error("Inference strategy does not support the stage");
}

void SyncInferenceStrategy::OnPushedOutputs() {
  is_finished_ = true;
}

void SyncInferenceStrategy::OnProcessFinished() {
  input_status_ = InputStatus::NEED_TO_READ;
  is_inference_active_ = false;
  is_finished_ = false;
}

} // namespace giga_edge