#include "giga_edge/modules/model/nodes/model/inference_strategies/queue_strategy.h"

namespace giga_edge {

namespace inference_strategy_factory_impl {
std::unique_ptr<IInferenceStrategy> CreateQueueStrategy(const InferenceStrategyParams& /* inference_strategy */) {
  return std::make_unique<QueueStrategy>();
}
} // namespace inference_strategy_factory_impl

InferenceStrategyAction QueueStrategy::NextAction() {
  if (has_outputs_pushed_) {
    OnProcessFinished();
    return InferenceStrategyAction::FINISH_PROCESSING;
  }

  if (input_status_ == InputStatus::NEED_TO_READ) {
    return InferenceStrategyAction::READ_INPUTS;
  }

  if (input_status_ == InputStatus::NOT_EMPTY) {
    input_status_ = InputStatus::EMPTY;
    return InferenceStrategyAction::ENQUEUE_INFERENCE;
  }

  if (force_wait_) {
    force_wait_ = false;
    return InferenceStrategyAction::WAIT_QUEUE;
  }

  return InferenceStrategyAction::PUSH_QUEUED_OUTPUTS;
}

void QueueStrategy::OnReset() {
  OnProcessFinished();
}

void QueueStrategy::OnInputs() {
  input_status_ = InputStatus::NOT_EMPTY;
}

void QueueStrategy::OnEmptyInputs() {
  input_status_ = InputStatus::EMPTY;
}

void QueueStrategy::OnInferred() {}

void QueueStrategy::OnForceWait() {
  force_wait_ = true;
}

void QueueStrategy::OnPushedOutputs() {
  has_outputs_pushed_ = true;
}

void QueueStrategy::OnProcessFinished() {
  input_status_ = InputStatus::NEED_TO_READ;
  has_outputs_pushed_ = false;
  force_wait_ = false;
}

} // namespace giga_edge
