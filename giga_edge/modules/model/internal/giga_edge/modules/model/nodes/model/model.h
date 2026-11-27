#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/model/models/model.h"
#include "giga_edge/modules/model/nodes/model/inference_strategies/inference_strategy.h"
#include "giga_edge/modules/model/nodes/model/model_params.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

namespace giga_edge {

class ModelNode : public NodeBase {
public:
  ModelNode(const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const ModelNodeParams& params);
  ~ModelNode() override;

  void ProcessOnce() override;
  void Reset() override;

  REGISTER_OPTIONAL_INPUT(model, need_reset, NeedReset, bool, "Whether to reset model before application");
  REGISTER_OPTIONAL_INPUT(
    model,
    force_wait,
    ForceWait,
    bool,
    "Only the wait_till_inferring and queue strategies wait, the others ignore it. wait_till_inferring waits for "
    "the inference in flight instead of leaving it for a later input. If the node was busy, this input itself is "
    "inferred asynchronously and comes out with the next input: follow it with an empty input with force_wait to "
    "get it now (flush_tail of feature_iterator_with_reset). queue waits for the whole queue, its outputs come out "
    "one per input: follow with max_queue such inputs."
  );

  REGISTER_DYNAMIC_INPUTS(model);
  REGISTER_DYNAMIC_OUTPUTS(model);

  void FillStatistics(SystemStatistics& statistics) const override;

private:
  void ProcessActions();

  bool IsScalarInput(const std::string& input);
  void ReadInputs();
  void ApplyShapeInputs();
  void InvokeSyncInference();
  void InvokeAsyncInference();
  void CheckInference();
  void WaitInference();
  void RethrowAsyncInferenceException();

  void PushOutputs();
  void PushEmptyOutputs();

  void CancelThreadWork();

  void EnqueueInference();
  void WaitQueue();
  void PushQueuedOutputs();
  void RunQueue();
  void CancelQueue();
  void WarnTailLeft(size_t outputs) const;

  void AddTimer(std::unique_ptr<BatchStatistics>& statistics, uint32_t& seq_id);
  void TriggerTimer(std::unique_ptr<BatchStatistics>& statistics, uint32_t seq_id);

private:
  Model model_;
  std::vector<std::string> inputs_;
  std::vector<std::string> outputs_;

  std::unordered_map<std::string, std::shared_ptr<const std::vector<float>>> input_data_map_;
  std::unordered_map<std::string, std::shared_ptr<const std::vector<int>>> shape_data_map_;
  std::unordered_map<std::string, bool> scalar_inputs_;
  std::unordered_map<std::string, std::string> shape_inputs_;

  std::shared_ptr<IThreadPool> thread_pool_ = nullptr;
  TaskPtr thread_pool_task_ = nullptr;
  int thread_priority_ = 0;
  std::exception_ptr async_inference_exception_ = nullptr;

  std::string invoke_statistics_id_;
  std::unique_ptr<BatchStatistics> invoke_statistics_ = nullptr;
  uint32_t invoke_seq_id_ = 0;
  std::string async_inference_statistics_id_;
  std::unique_ptr<BatchStatistics> async_inference_statistics_ = nullptr;
  uint32_t async_inference_seq_id_ = 0;

  std::unique_ptr<IInferenceStrategy> strategy_;

  using InputDataMap = std::unordered_map<std::string, std::shared_ptr<const std::vector<float>>>;
  using OutputDataMap = std::unordered_map<std::string, std::vector<float>>;
  struct InferenceQueue {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<InputDataMap> pending;
    std::deque<OutputDataMap> ready;
    TaskPtr task;
    bool running = false;
    bool inferring = false;
    std::exception_ptr error;
  };
  InferenceQueue queue_;
  size_t max_queue_ = 1;
  bool flushing_ = false;

  std::unordered_map<InferenceStrategyAction, std::function<void(ModelNode*)>> actions_ = {
    {InferenceStrategyAction::READ_INPUTS, &ModelNode::ReadInputs},
    {InferenceStrategyAction::SYNC_INFERENCE, &ModelNode::InvokeSyncInference},
    {InferenceStrategyAction::ASYNC_INFERENCE, &ModelNode::InvokeAsyncInference},
    {InferenceStrategyAction::PUSH_OUTPUTS, &ModelNode::PushOutputs},
    {InferenceStrategyAction::PUSH_EMPTY_OUTPUTS, &ModelNode::PushEmptyOutputs},
    {InferenceStrategyAction::CHECK_INFERENCE, &ModelNode::CheckInference},
    {InferenceStrategyAction::WAIT_INFERENCE, &ModelNode::WaitInference},
    {InferenceStrategyAction::ENQUEUE_INFERENCE, &ModelNode::EnqueueInference},
    {InferenceStrategyAction::WAIT_QUEUE, &ModelNode::WaitQueue},
    {InferenceStrategyAction::PUSH_QUEUED_OUTPUTS, &ModelNode::PushQueuedOutputs},
  };
};

REGISTER_NODE(model, ModelNode, ModelNodeParams, true, "Applies model");

} // namespace giga_edge
