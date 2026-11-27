#pragma once

#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"

namespace giga_edge {

class WaitNumberOfChunksStrategy : public IInferenceStrategy {
public:
  WaitNumberOfChunksStrategy() = default;

  explicit WaitNumberOfChunksStrategy(const WaitNumberOfChunksStrategyParams& params)
    : chunk_cap_(params.chunk_cap) {}

  InferenceStrategyAction NextAction() override;

  void OnReset() override;
  void OnInputs() override;
  void OnEmptyInputs() override;
  void OnInferred() override;
  void OnPushedOutputs() override;

private:
  void OnProcessFinished() override;

private:
  InferenceState inference_state_ = InferenceState::IDLE;
  int chunk_cap_ = 1;
  int chunk_counter_ = -1;

  InputStatus input_status_ = InputStatus::NEED_TO_READ;
  bool has_outputs_pushed_ = false;
  bool is_finished_ = false;
};

} // namespace giga_edge