#pragma once

#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"

namespace giga_edge {

// wait_till_inferring with a queue of depth max_queue: the tick does not wait, one output per input, in input order.
class QueueStrategy : public IInferenceStrategy {
public:
  QueueStrategy() = default;

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
  InputStatus input_status_ = InputStatus::NEED_TO_READ;
  bool has_outputs_pushed_ = false;
  bool force_wait_ = false;
};

} // namespace giga_edge
