#include "giga_edge/modules/model/nodes/model/inference_strategies/wait_number_of_chunks_strategy.h"

#include <iostream>

namespace giga_edge {

namespace inference_strategy_factory_impl {
std::unique_ptr<IInferenceStrategy> CreateWaitNumberOfChunksStrategy(
  const InferenceStrategyParams& inference_strategy
) {
  return std::make_unique<WaitNumberOfChunksStrategy>(inference_strategy.wait_number_of_chunks_strategy_params);
}
} // namespace inference_strategy_factory_impl

InferenceStrategyAction WaitNumberOfChunksStrategy::NextAction() {
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

  if (chunk_counter_ == chunk_cap_) {
    chunk_counter_ = -1;

    return InferenceStrategyAction::WAIT_INFERENCE;
  }

  if (chunk_counter_ > 0) {
    if (input_status_ == InputStatus::NOT_EMPTY) {
      std::cerr << "[WARNING] Skip of significant data, try to regulate \'chunk_cap\' parameter to avoid the drops"
                << std::endl;
    }
    return InferenceStrategyAction::PUSH_EMPTY_OUTPUTS;
  }

  if (inference_state_ == InferenceState::INFERRED) {
    return InferenceStrategyAction::PUSH_OUTPUTS;
  }

  if (inference_state_ == InferenceState::IDLE && input_status_ == InputStatus::NOT_EMPTY) {
    inference_state_ = InferenceState::INFERRING;
    input_status_ = InputStatus::EMPTY;
    return InferenceStrategyAction::ASYNC_INFERENCE;
  }

  return InferenceStrategyAction::PUSH_EMPTY_OUTPUTS;
}

void WaitNumberOfChunksStrategy::OnReset() {
  inference_state_ = InferenceState::IDLE;
  chunk_counter_ = -1;

  OnProcessFinished();
}

void WaitNumberOfChunksStrategy::OnInputs() {
  input_status_ = InputStatus::NOT_EMPTY;
  chunk_counter_++;
}

void WaitNumberOfChunksStrategy::OnEmptyInputs() {
  input_status_ = InputStatus::EMPTY;
  if (inference_state_ != InferenceState::IDLE) {
    chunk_counter_++;
  }
}

void WaitNumberOfChunksStrategy::OnInferred() {
  chunk_counter_ = -1;

  if (input_status_ == InputStatus::NOT_EMPTY) {
    chunk_counter_++;
  }

  inference_state_ = InferenceState::INFERRED;
}

void WaitNumberOfChunksStrategy::OnPushedOutputs() {
  has_outputs_pushed_ = true;
  is_finished_ = !(input_status_ == InputStatus::NOT_EMPTY && inference_state_ == InferenceState::INFERRED);
}

void WaitNumberOfChunksStrategy::OnProcessFinished() {
  if (inference_state_ == InferenceState::INFERRED) {
    inference_state_ = InferenceState::IDLE;
  }

  input_status_ = InputStatus::NEED_TO_READ;
  has_outputs_pushed_ = false;
  is_finished_ = false;
}

} // namespace giga_edge