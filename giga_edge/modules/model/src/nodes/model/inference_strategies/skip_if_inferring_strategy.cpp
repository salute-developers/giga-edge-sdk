#include "giga_edge/modules/model/nodes/model/inference_strategies/skip_if_inferring_strategy.h"

namespace giga_edge {

namespace inference_strategy_factory_impl {
std::unique_ptr<IInferenceStrategy> CreateSkipIfInferringStrategy(const InferenceStrategyParams& inference_strategy) {
  return std::make_unique<SkipIfInferringStrategy>();
}
} // namespace inference_strategy_factory_impl

InferenceStrategyAction SkipIfInferringStrategy::NextAction() {
  if (is_finished_) {
    OnProcessFinished();
    return InferenceStrategyAction::FINISH_PROCESSING;
  }

  if (input_status_ == InputStatus::NEED_TO_READ) {
    return InferenceStrategyAction::READ_INPUTS;
  }

  if (has_outputs_pushed_) {
    inference_state_ = InferenceState::INFERRING;
    is_finished_ = true;
    return InferenceStrategyAction::ASYNC_INFERENCE;
  }

  if (inference_state_ == InferenceState::INFERRED) {
    return InferenceStrategyAction::PUSH_OUTPUTS;
  }

  if (inference_state_ == InferenceState::INFERRING) {
    if (is_check_required_) {
      is_check_required_ = false;
      return InferenceStrategyAction::CHECK_INFERENCE;
    } else {
      return InferenceStrategyAction::PUSH_EMPTY_OUTPUTS;
    }
  }

  if (inference_state_ == InferenceState::IDLE && input_status_ == InputStatus::NOT_EMPTY) {
    inference_state_ = InferenceState::INFERRING;
    is_check_required_ = false;
    input_status_ = InputStatus::EMPTY;
    return InferenceStrategyAction::ASYNC_INFERENCE;
  }

  return InferenceStrategyAction::PUSH_EMPTY_OUTPUTS;
}

void SkipIfInferringStrategy::OnReset() {
  OnProcessFinished();
  inference_state_ = InferenceState::IDLE;
}

void SkipIfInferringStrategy::OnInputs() {
  input_status_ = InputStatus::NOT_EMPTY;
}

void SkipIfInferringStrategy::OnEmptyInputs() {
  input_status_ = InputStatus::EMPTY;
}

void SkipIfInferringStrategy::OnInferred() {
  inference_state_ = InferenceState::INFERRED;
}

void SkipIfInferringStrategy::OnPushedOutputs() {
  has_outputs_pushed_ = true;
  is_finished_ = !(input_status_ == InputStatus::NOT_EMPTY && inference_state_ == InferenceState::INFERRED);
}

void SkipIfInferringStrategy::OnProcessFinished() {
  if (inference_state_ == InferenceState::INFERRED) {
    inference_state_ = InferenceState::IDLE;
  }

  input_status_ = InputStatus::NEED_TO_READ;
  is_check_required_ = true;
  has_outputs_pushed_ = false;
  is_finished_ = false;
}

} // namespace giga_edge