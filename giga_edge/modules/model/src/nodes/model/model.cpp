#include "giga_edge/modules/model/nodes/model/model.h"

#include "giga_edge/lib/common/span.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/modules/model/models/model_params.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace giga_edge {

ModelNode::ModelNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const ModelNodeParams& params
)
  : NodeBase(base_config)
  , model_(params, params.path, data_reader, base_config.environments)
  , invoke_statistics_id_(id_ + "::model_invoke")
  , invoke_statistics_(
      base_config.common_node_params.statistics_length
        ? std::make_unique<BatchStatistics>(invoke_statistics_id_, base_config.common_node_params.statistics_length)
        : nullptr
    )
  , strategy_(InferenceStrategyFactory::CreateInferenceStrategy(params.inference_strategy)) {
  const auto& inference_strategy_params = params.inference_strategy;
  if (inference_strategy_params.type != InferenceStrategyType::SYNC_INFERENCE) {
    if (
      base_config.common_node_params.statistics_length && inference_strategy_params.type != InferenceStrategyType::QUEUE
    ) {
      async_inference_statistics_id_ = id_ + "::async_inference";
      async_inference_statistics_ = std::make_unique<BatchStatistics>(
        async_inference_statistics_id_, base_config.common_node_params.statistics_length
      );
    }

    const auto& async_strategy_params = inference_strategy_params.async_strategy_params;
    const auto& thread_pool_name = async_strategy_params.thread_pool_name;

    const auto& environment_it = base_config.environments->find(thread_pool_name);
    if (environment_it == base_config.environments->end()) {
      std::stringstream ss;
      ss << "\'thread_pool_name\' (" << thread_pool_name << ") does not refer to existing thread pool environment";
      throw std::runtime_error(ss.str());
    } else {
      thread_pool_ = std::dynamic_pointer_cast<ThreadPoolEnvironment>(environment_it->second)->thread_pool;
    }
    thread_priority_ = async_strategy_params.thread_priority;
  }
  if (inference_strategy_params.type == InferenceStrategyType::QUEUE) {
    max_queue_ = inference_strategy_params.queue_strategy_params.max_queue;
    if (!params.shape_inputs.empty()) {
      throw std::runtime_error(id_ + ": queue inference strategy does not support shape inputs");
    }
  }
  for (const auto& input : model_.GetInputNames()) {
    if (params.const_inputs.find(input) == params.const_inputs.end()) {
      inputs_.push_back(input);
    }
  }

  shape_inputs_ = params.shape_inputs;

  outputs_ = model_.GetOutputNames();

  model_.Reset();
}

ModelNode::~ModelNode() {
  CancelThreadWork();
  CancelQueue();
}

void ModelNode::ProcessOnce() {
  const auto need_reset = GetNeedReset();
  if (need_reset.has_value() && need_reset.value()) {
    Reset();
  }

  const auto force_wait = GetForceWait();
  if (force_wait.has_value() && force_wait.value()) {
    strategy_->OnForceWait();
  }

  ProcessActions();
}

void ModelNode::Reset() {
  CancelThreadWork();
  CancelQueue();
  model_.Reset();
  strategy_->OnReset();
}

void ModelNode::ProcessActions() {
  while (true) {
    const auto next_action = strategy_->NextAction();

    if (next_action == InferenceStrategyAction::FINISH_PROCESSING) {
      return;
    }
    actions_[next_action](this);
  }
}

bool ModelNode::IsScalarInput(const std::string& input) {
  if (const auto it = scalar_inputs_.find(input); it != scalar_inputs_.end()) {
    return it->second;
  }
  // The input type is found out once: InputsData cannot check a type without throwing.
  bool scalar = false;
  try {
    inputs_data_->GetSingleShared<std::vector<float>>(input);
  } catch (const std::runtime_error&) {
    scalar = true;
  }
  scalar_inputs_.emplace(input, scalar);
  return scalar;
}

void ModelNode::ReadInputs() {
  std::optional<bool> empty_inputs;
  input_data_map_.clear();
  std::unordered_map<std::string, std::shared_ptr<const std::vector<float>>> scalars;

  for (const auto& input : inputs_) {
    // A scalar (e.g. a length from math_expression) goes as a one-element tensor and only together with vectors.
    if (IsScalarInput(input)) {
      scalars[input] = std::make_shared<const std::vector<float>>(1, inputs_data_->GetSingle<float>(input));
      continue;
    }

    auto data = inputs_data_->GetSingleShared<std::vector<float>>(input);
    if (!empty_inputs.has_value()) {
      empty_inputs = data->empty();
    } else if (empty_inputs.value() != data->empty()) {
      throw std::runtime_error(id_ + ": some inputs have data, while others not");
    }

    if (empty_inputs.value()) {
      continue;
    }

    input_data_map_[input] = std::move(data);
  }

  if (!empty_inputs.value_or(false)) {
    input_data_map_.merge(scalars);
  }

  if (input_data_map_.empty()) {
    strategy_->OnEmptyInputs();
    return;
  }

  for (const auto& [shape_input, model_input] : shape_inputs_) {
    auto shape = inputs_data_->GetSingleShared<std::vector<int>>(shape_input);
    if (!shape->empty()) {
      // Applied at the start of an Invoke action, where no inference task can
      // be reading the tensors being resized.
      shape_data_map_[model_input] = std::move(shape);
    }
  }

  strategy_->OnInputs();
}

void ModelNode::ApplyShapeInputs() {
  for (auto& [model_input, shape] : shape_data_map_) {
    model_.ResizeInput(model_input, *shape);
  }
  shape_data_map_.clear();
}

void ModelNode::InvokeSyncInference() {
  AddTimer(invoke_statistics_, invoke_seq_id_);

  ApplyShapeInputs();
  model_.SetInputData(input_data_map_);
  model_.Invoke();

  TriggerTimer(invoke_statistics_, invoke_seq_id_);
}

void ModelNode::InvokeAsyncInference() {
  AddTimer(async_inference_statistics_, async_inference_seq_id_);

  ApplyShapeInputs();
  model_.SetInputData(input_data_map_);

  auto thread_work = [this]() {
    // An exception escaping the pool worker thread calls std::terminate; keep
    // it here and rethrow on the node thread once the task completes.
    try {
      AddTimer(invoke_statistics_, invoke_seq_id_);
      model_.Invoke();
      TriggerTimer(invoke_statistics_, invoke_seq_id_);
    } catch (...) {
      async_inference_exception_ = std::current_exception();
    }
  };

  if (thread_pool_) {
    thread_pool_task_ = std::make_shared<Task>(thread_work, thread_priority_);
    thread_pool_->Push(thread_pool_task_);
  }
}

void ModelNode::CheckInference() {
  assert(thread_pool_task_);
  if (thread_pool_task_->status == Task::Status::DONE) {
    strategy_->OnInferred();
    TriggerTimer(async_inference_statistics_, async_inference_seq_id_);
    thread_pool_task_ = nullptr;
    RethrowAsyncInferenceException();
  }
}

void ModelNode::WaitInference() {
  assert(thread_pool_task_);
  thread_pool_->Wait(thread_pool_task_);
  if (thread_pool_task_->status == Task::Status::DONE) {
    strategy_->OnInferred();
  }
  TriggerTimer(async_inference_statistics_, async_inference_seq_id_);
  thread_pool_task_ = nullptr;
  RethrowAsyncInferenceException();
}

void ModelNode::RethrowAsyncInferenceException() {
  const auto exception = std::exchange(async_inference_exception_, nullptr);
  if (!exception) {
    return;
  }
  // The failed processing cycle is over: put the strategy back into a clean
  // state before the exception unwinds further processing.
  strategy_->OnReset();
  std::rethrow_exception(exception);
}

void ModelNode::PushOutputs() {
  strategy_->OnPushedOutputs();

  auto output_data_map = model_.GetOutputData<float>(outputs_);
  for (auto& [output, data] : output_data_map) {
    outputs_data_->Add<std::vector<float>>(output, std::move(data));
  }
}

void ModelNode::PushEmptyOutputs() {
  strategy_->OnPushedOutputs();

  for (const auto& output : outputs_) {
    outputs_data_->Add<std::vector<float>>(output, {});
  }
}

void ModelNode::EnqueueInference() {
  TaskPtr task;
  {
    std::unique_lock lock(queue_.mutex);
    // The queue is full: wait for the oldest output, it is the one that comes out on this input.
    if (queue_.pending.size() + queue_.ready.size() + (queue_.inferring ? 1 : 0) >= max_queue_) {
      queue_.changed.wait(lock, [this] {
        return !queue_.ready.empty() || queue_.error;
      });
    }
    if (queue_.error) {
      std::rethrow_exception(std::exchange(queue_.error, nullptr));
    }
    queue_.pending.push_back(input_data_map_);
    if (!queue_.running) {
      queue_.running = true;
      task = queue_.task = std::make_shared<Task>(
        [this] {
          RunQueue();
        },
        thread_priority_
      );
    }
  }
  if (task) {
    thread_pool_->Push(task);
  }
}

void ModelNode::WaitQueue() {
  std::unique_lock lock(queue_.mutex);
  queue_.changed.wait(lock, [this] {
    return !queue_.running || queue_.error;
  });
  flushing_ = true;
}

void ModelNode::PushQueuedOutputs() {
  const auto force_wait = GetForceWait();
  const bool forced = force_wait.has_value() && force_wait.value().get();
  std::optional<OutputDataMap> output;
  {
    std::lock_guard lock(queue_.mutex);
    if (queue_.error) {
      std::rethrow_exception(std::exchange(queue_.error, nullptr));
    }
    if (flushing_ && !forced && !queue_.ready.empty()) {
      WarnTailLeft(queue_.ready.size());
    }
    flushing_ = flushing_ && forced;
    if (!queue_.ready.empty()) {
      output = std::move(queue_.ready.front());
      queue_.ready.pop_front();
    }
  }
  strategy_->OnPushedOutputs();

  for (const auto& name : outputs_) {
    outputs_data_->Add<std::vector<float>>(name, output ? std::move((*output)[name]) : std::vector<float>{});
  }
}

void ModelNode::RunQueue() {
  while (true) {
    InputDataMap input;
    {
      std::lock_guard lock(queue_.mutex);
      if (queue_.pending.empty()) {
        queue_.running = false;
        queue_.changed.notify_all();
        return;
      }
      input = std::move(queue_.pending.front());
      queue_.pending.pop_front();
      queue_.inferring = true;
    }

    OutputDataMap output;
    try {
      AddTimer(invoke_statistics_, invoke_seq_id_);
      model_.SetInputData(input);
      model_.Invoke();
      TriggerTimer(invoke_statistics_, invoke_seq_id_);
      output = model_.GetOutputData<float>(outputs_);
    } catch (...) {
      std::lock_guard lock(queue_.mutex);
      queue_.error = std::current_exception();
      queue_.pending.clear();
      queue_.inferring = false;
      queue_.running = false;
      queue_.changed.notify_all();
      return;
    }

    std::lock_guard lock(queue_.mutex);
    queue_.ready.push_back(std::move(output));
    queue_.inferring = false;
    queue_.changed.notify_all();
  }
}

void ModelNode::CancelQueue() {
  TaskPtr task;
  {
    std::lock_guard lock(queue_.mutex);
    queue_.pending.clear();
    if (queue_.running) {
      task = queue_.task;
    }
  }
  if (task && !thread_pool_->Cancel(task)) {
    // The task is already running: it finishes the current input and exits on the empty queue.
    thread_pool_->Wait(task);
  }
  std::lock_guard lock(queue_.mutex);
  if (flushing_ && !queue_.ready.empty()) {
    WarnTailLeft(queue_.ready.size());
  }
  queue_.running = false;
  queue_.ready.clear();
  queue_.task = nullptr;
  queue_.error = nullptr;
  flushing_ = false;
}

void ModelNode::WarnTailLeft(size_t outputs) const {
  std::cerr << "[WARNING] " << id_ << ": " << outputs
            << " outputs left after force_wait, raise the number of flushing inputs" << std::endl;
}

void ModelNode::CancelThreadWork() {
  if (thread_pool_ && thread_pool_task_) {
    if (!thread_pool_->Cancel(thread_pool_task_)) {
      thread_pool_->Wait(thread_pool_task_);
    }
    TriggerTimer(async_inference_statistics_, async_inference_seq_id_);
    thread_pool_task_ = nullptr;
  }
  if (async_inference_exception_) {
    std::cerr << "[WARNING] " << id_ << ": discarded a failed asynchronous inference on reset" << std::endl;
    TriggerTimer(invoke_statistics_, invoke_seq_id_);
  }
  // Reset and destruction abort the failed cycle, discarding its exception
  // and its not-yet-applied shapes.
  async_inference_exception_ = nullptr;
  shape_data_map_.clear();
}

std::unordered_set<std::string> ModelNode::GetDynamicInputs() {
  auto inputs = std::unordered_set<std::string>(inputs_.begin(), inputs_.end());
  for (const auto& [shape_input, model_input] : shape_inputs_) {
    inputs.insert(shape_input);
  }
  return inputs;
}

std::unordered_set<std::string> ModelNode::GetDynamicOutputs() {
  return {outputs_.begin(), outputs_.end()};
}

void ModelNode::FillStatistics(SystemStatistics& statistics) const {
  NodeBase::FillStatistics(statistics);

  if (invoke_statistics_) {
    invoke_statistics_->Fill(statistics);
    statistics.AddNodePriority(invoke_statistics_id_, priority_);
  }
  if (async_inference_statistics_) {
    async_inference_statistics_->Fill(statistics);
    statistics.AddNodePriority(async_inference_statistics_id_, priority_);
  }
}

void ModelNode::AddTimer(std::unique_ptr<BatchStatistics>& statistics, uint32_t& seq_id) {
  if (statistics) {
    ++seq_id;
    statistics->AddTimer(seq_id);
  }
}

void ModelNode::TriggerTimer(std::unique_ptr<BatchStatistics>& statistics, uint32_t seq_id) {
  if (statistics) {
    statistics->TriggerTimer(seq_id);
  }
}

} // namespace giga_edge
