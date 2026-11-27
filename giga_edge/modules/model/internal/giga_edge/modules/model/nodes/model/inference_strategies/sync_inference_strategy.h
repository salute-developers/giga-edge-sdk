#pragma once

#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"

namespace giga_edge {

class SyncInferenceStrategy : public IInferenceStrategy {
public:
  SyncInferenceStrategy() = default;

  InferenceStrategyAction NextAction() override;

  void OnReset() override;
  void OnInputs() override;
  void OnEmptyInputs() override;
  void OnInferred() override;
  void OnPushedOutputs() override;

private:
  void OnProcessFinished() override;

private:
  InputStatus input_status_ = InputStatus::NEED_TO_READ;
  bool is_inference_active_ = false;
  bool is_finished_ = false;
};

} // namespace giga_edge