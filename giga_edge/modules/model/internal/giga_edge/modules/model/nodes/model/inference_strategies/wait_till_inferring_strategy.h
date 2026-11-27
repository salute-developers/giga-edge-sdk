#pragma once

#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"

namespace giga_edge {

class WaitTillInferringStrategy : public IInferenceStrategy {
public:
  WaitTillInferringStrategy() = default;

  InferenceStrategyAction NextAction() override;

  void OnReset() override;
  void OnInputs() override;
  void OnEmptyInputs() override;
  void OnInferred() override;
  void OnPushedOutputs() override;
  void OnForceWait() override;

private:
  void OnProcessFinished() override;

private:
  InferenceState inference_state_ = InferenceState::IDLE;

  InputStatus input_status_ = InputStatus::NEED_TO_READ;
  bool is_check_required_ = true;
  bool has_outputs_pushed_ = false;
  bool is_finished_ = false;
  bool force_wait_ = false;
};

} // namespace giga_edge