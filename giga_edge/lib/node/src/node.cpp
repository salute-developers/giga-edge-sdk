#include "giga_edge/lib/node/node.h"

#include "giga_edge/lib/common/str_utils.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/node/event.h"
#include "giga_edge/lib/node/loop_scheduler.h"
#include "giga_edge/lib/node/system_events.h"

#include <cassert>
#include <optional>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(
  NextProcessingType, "none", "inputs", "event", "skip_inputs", "virtual_loop_iteration", "virtual_loop_interrupt"
);

namespace giga_edge {

namespace {

// To allow passing same node output into multiple inputs.
NodeId MergeIds(const NodeId& node_id, const std::string& input_name) {
  return node_id + input_name;
}

NodeId GenerateNodeId(const NodeId& id, const SubmodelVersionChain& submodel_version_chain) {
  return submodel_version_chain.ids_chain.empty() ? id : submodel_version_chain.AsString() + "@" + id;
}

// -1 - before, 0 - same, +1 - after.
int32_t Order(uint32_t f, uint32_t s) {
  if (f == s) {
    return 0;
  } else if ((s - f) < 0x8000'0000) {
    return -1;
  } else {
    return 1;
  }
}

} // namespace

NodeBase::NodeBase(const NodeBaseConfig& config)
  : id_(GenerateNodeId(config.base_id, config.submodel_version_chain))
  , priority_(config.priority)
  , inputs_(config.inputs)
  , outputs_(config.outputs)
  , events_(config.events)
  , next_seq_id_(config.next_seq_id)
  , output_batch_id_(0)
  , enqueue_callback_(config.enqueue_callback)
  , unqueue_callback_(config.unqueue_callback)
  , statistics_(
      config.common_node_params.statistics_length
        ? std::make_unique<BatchStatistics>(Id(), config.common_node_params.statistics_length)
        : nullptr
    )
  , external_inputs_(config.external_inputs)
  , submodel_version_chain_(config.submodel_version_chain)
  , status_{config.submodel_version_chain.ids_chain.empty() ? NodeStatus::ACTIVE : NodeStatus::INACTIVE}
  , is_started_(false) {
  for ([[maybe_unused]] const auto& external_input : external_inputs_) {
    assert(inputs_.find(external_input) != inputs_.end());
  }
}

void NodeBase::Start() {
  is_started_ = true;

  for (const auto& [input_id, input] : inputs_) {
    const auto merged_id = MergeIds(id_, input_id);
    input->Subscribe(merged_id, [this]() {
      Enqueue();
    });
  }

  events_->Subscribe(id_, [this]() {
    Enqueue();
  });
}

bool NodeBase::IsStarted() const {
  return is_started_;
}

const NodeId& NodeBase::Id() const {
  return id_;
};

void NodeBase::InitRecursiveOutputs() {
  if (recursive_outputs_.empty()) {
    return;
  }
  std::vector<std::string_view> recursive_outputs = {recursive_outputs_.begin(), recursive_outputs_.end()};
  outputs_data_ = std::make_unique<OutputsData>(
    id_, ValueInfo{}, 0, recursive_outputs, batch_outputs_, recursive_outputs_
  );
  DoInitRecursiveOutputs();
  CloseOutputs();
  PushOutputs();
}

void NodeBase::DoInitRecursiveOutputs() {
  throw std::runtime_error("Node " + Id() + "does not initialize it's recursive_outputs");
}

void NodeBase::ConsumeInputs() {
  std::unordered_map<std::string, OutputValueBasePtr> single_data;
  std::unordered_map<std::string, std::vector<OutputValueBasePtr>> batch_data;

  for (const auto& [input_id, input] : inputs_) {
    const auto merged_id = MergeIds(id_, input_id);
    if (IsBatchInput(input_id)) {
      batch_data[input_id] = input->ConsumeBatch(merged_id);
    } else {
      single_data[input_id] = input->ConsumeSingle(merged_id);
    }
  }

  inputs_data_ = std::make_unique<InputsData>(id_, std::move(single_data), std::move(batch_data));
}

void NodeBase::InitOutputsData() {
  std::vector<std::string_view> outputs;
  outputs.reserve(outputs_.size());
  for (const auto& [id, output] : outputs_) {
    outputs.push_back(id);
  }

  outputs_data_ = std::make_unique<OutputsData>(
    id_, inputs_data_->GetValueInfo(), output_batch_id_++, outputs, batch_outputs_, recursive_outputs_
  );
}

void NodeBase::CloseOutputs() {
  outputs_data_->Close();
}

void NodeBase::PushOutputs() {
  auto data = outputs_data_->ExtractData();

  for (const auto& [id, output] : data) {
    for (const auto& item : data.at(id)) {
      outputs_.at(std::string(id))->Push(item);
    }
  }
}

void NodeBase::SetRemoved() {
  status_ = NodeStatus::REMOVED;
}

void NodeBase::SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler) {
  loop_scheduler_ = scheduler;
}

void NodeBase::UnsubscribeExternalInputs(const std::unordered_set<NodeOutputPtr>& inputs) {
  std::vector<std::pair<NodeOutputId, NodeOutputPtr>> external_inputs;

  for (const auto& [input_id, input] : inputs_) {
    if (inputs.find(input) != inputs.end()) {
      external_inputs.emplace_back(input_id, input);
    }
  }

  unqueue_callback_(shared_from_this());

  events_->Unsubscribe(id_);
  for (const auto& [input_id, input] : external_inputs) {
    input->Unsubscribe(MergeIds(id_, input_id));
  }
}

NextProcessingType NodeBase::GetNextProcessingTypeInactive() const {
  if (events_->HasNext(id_)) {
    return NextProcessingType::EVENT;
  }

  for (const auto& input_id : external_inputs_) {
    const auto merged_id = MergeIds(id_, input_id);
    auto input = inputs_.at(input_id);

    if (input->HasNext(merged_id) && Order(input->PeekValueInfo(merged_id).seq_id, next_seq_id_) <= 0) {
      return NextProcessingType::SKIP_INPUTS;
    }
  }
  return NextProcessingType::NONE;
}

NextProcessingType NodeBase::GetNextProcessingTypeActive() const {
  // Inputs are published before events. If a following input is visible, the preceding event is visible too.
  // Checking inputs first prevents consuming that input and permanently skipping the event.
  bool have_all_inputs = true;
  bool skip_inputs = false;
  for (const auto& [input_id, input] : inputs_) {
    const auto merged_id = MergeIds(id_, input_id);
    if (!input->HasNext(merged_id, IsBatchInput(input_id))) {
      have_all_inputs = false;
    }

    if (input->HasNext(merged_id)) {
      const auto& value_info = input->PeekValueInfo(merged_id);
      if (!value_info.is_recursive && Order(value_info.seq_id, next_seq_id_) < 0) {
        assert(external_inputs_.find(input_id) != external_inputs_.end());
        skip_inputs = true;
      }
    }
  }

  const bool has_pending_event = events_->HasNext(id_) && events_->PeekValueInfo(id_).seq_id == next_seq_id_;

  if (has_pending_event && !IsInLoop()) {
    return NextProcessingType::EVENT;
  }
  if (skip_inputs) {
    return NextProcessingType::SKIP_INPUTS;
  }

  if (IsInLoop()) {
    const bool live_interrupt = have_all_inputs || has_pending_event;
    const bool interrupt = (loop_scheduler_ && inputs_data_)
      ? loop_scheduler_->DecideInterrupt(id_, next_seq_id_ - 1, live_interrupt)
      : live_interrupt;
    return interrupt ? NextProcessingType::LOOP_INTERRUPT : NextProcessingType::LOOP_ITERATION;
  }
  return have_all_inputs ? NextProcessingType::INPUTS : NextProcessingType::NONE;
}

NextProcessingType NodeBase::GetNextProcessingType() const {
  assert(status_ != NodeStatus::REMOVED);
  auto result = IsActive() ? GetNextProcessingTypeActive() : GetNextProcessingTypeInactive();
  return result;
}

void NodeBase::ProcessSkipInactiveInputs() {
  for (const auto& input_id : external_inputs_) {
    const auto merged_id = MergeIds(id_, input_id);
    auto input = inputs_.at(input_id);
    while (input->HasNext(merged_id)) {
      const auto& value_info = input->PeekValueInfo(merged_id);
      if (value_info.is_recursive) {
        break;
      }
      auto order = Order(value_info.seq_id, next_seq_id_);
      if (order == -1) {
        input->ConsumeSingle(merged_id);
      } else if (order == 1) {
        break;
      } else { // order == 0
        if (IsActive()) {
          break;
        } else {
          input->ConsumeSingle(merged_id);
          ++next_seq_id_;
        }
      }
    }
  }
}

void NodeBase::ProcessInactive() {
  for (int i = 0; i < GetMaxConsecutiveIterations() && !IsActive(); ++i) {
    switch (auto processing_type = GetNextProcessingTypeInactive()) {
      case NextProcessingType::EVENT:
        ProcessEvent();
        break;
      case NextProcessingType::SKIP_INPUTS:
        ProcessSkipInactiveInputs();
        break;
      case NextProcessingType::NONE:
        assert(i != 0);
        return;
      default:
        throw std::runtime_error(
          "Unsupported NextProcessingType in ProcessInactive: " + std::string{EnumToString(processing_type)}
        );
    }
  }
}

void NodeBase::AddTimer(int32_t id) {
  if (statistics_) {
    statistics_->AddTimer(id);
  }
}

void NodeBase::TriggerTimer(int32_t id) {
  if (statistics_) {
    statistics_->TriggerTimer(id);
  }
}

void NodeBase::ProcessInputs() {
  int external_id = 0;

  if (statistics_) {
    for (const auto& [input_id, input] : inputs_) {
      const auto merged_id = MergeIds(id_, input_id);
      if (const auto& value_info = input->PeekValueInfo(merged_id); !value_info.is_recursive) {
        external_id = value_info.seq_id;
        break;
      }
    }
  }

  AddTimer(external_id);
  ConsumeInputs();
  InitOutputsData();
  ProcessOnce();
  if (!IsInLoop()) {
    CloseOutputs();
  }
  PushOutputs();
  TriggerTimer(external_id);

  if (inputs_data_->GetValueInfo().IsLastSeqInfo()) {
    ++next_seq_id_;
  }
}

void NodeBase::ProcessLoopIteration() {
  AddTimer(inputs_data_->GetValueInfo().seq_id);

  ProcessLoopIterationImpl();
  if (loop_scheduler_) {
    loop_scheduler_->OnIteration(id_, next_seq_id_ - 1);
  }
  PushOutputs();

  TriggerTimer(inputs_data_->GetValueInfo().seq_id);
}

void NodeBase::ProcessLoopInterrupt() {
  AddTimer(inputs_data_->GetValueInfo().seq_id);

  ProcessLoopInterruptImpl();
  FinishLoop();

  CloseOutputs();
  PushOutputs();

  TriggerTimer(inputs_data_->GetValueInfo().seq_id);
}

void NodeBase::ProcessActive() {
  // Event and skip_inputs are unblockable, but inputs iteration after them may be blocked,
  //   so we need to exit current processing and reschedule (see Runtime::Enqueue).
  bool need_reschedule_for_inputs = false;

  for (int i = 0; i < GetMaxConsecutiveIterations() && IsActive(); ++i) {
    switch (GetNextProcessingTypeActive()) {
      case NextProcessingType::EVENT:
        ProcessEvent();
        return;
      case NextProcessingType::SKIP_INPUTS:
        ProcessSkipInactiveInputs();
        need_reschedule_for_inputs = true;
        break;
      case NextProcessingType::INPUTS:
        if (need_reschedule_for_inputs || !CanPush()) {
          return;
        }
        ProcessInputs();
        break;
      case NextProcessingType::LOOP_ITERATION:
        ProcessLoopIteration();
        break;
      case NextProcessingType::LOOP_INTERRUPT:
        ProcessLoopInterrupt();
        break;
      case NextProcessingType::NONE:
        assert(i != 0);
        return;
    }
  }
}

void NodeBase::Process() {
  assert(status_ != NodeStatus::REMOVED);
  IsActive() ? ProcessActive() : ProcessInactive();
}

bool NodeBase::CanPush() const {
  for (const auto& [_, output] : outputs_) {
    if (output->Size() >= MAX_OUTPUT_SIZE) {
      return false;
    }
  }
  return true;
}

int32_t NodeBase::Priority() const {
  // To avoid spinning on a single node, check that the output is not full.
  return CanPush() ? priority_ : 0;
}

uint32_t NodeBase::NextSeqId() const {
  return next_seq_id_;
}

bool NodeBase::IsActive() const {
  return status_ == NodeStatus::ACTIVE;
}

NodeStatus NodeBase::Status() const {
  return status_;
}

const std::unordered_map<std::string, NodeOutputPtr>& NodeBase::Inputs() const {
  return inputs_;
}

const std::unordered_map<std::string, NodeOutputPtr>& NodeBase::Outputs() const {
  return outputs_;
}

bool NodeBase::IsBatchInput(const std::string& input_id) const {
  return batch_inputs_.find(input_id) != batch_inputs_.end();
}

bool NodeBase::IsBatchOutput(const std::string& output_id) const {
  return batch_outputs_.find(output_id) != batch_outputs_.end();
}

bool NodeBase::IsRecursiveOutput(const std::string& output_id) const {
  return recursive_outputs_.find(output_id) != recursive_outputs_.end();
}

void NodeBase::Enqueue() {
  enqueue_callback_(shared_from_this());
}

void NodeBase::FillStatistics(SystemStatistics& statistics) const {
  if (statistics_ != nullptr) {
    statistics_->Fill(statistics);
    statistics.AddNodePriority(Id(), priority_);
  }
}

void NodeBase::SetConnectionsInfo(
  const std::unordered_set<std::string_view>& batch_inputs,
  const std::unordered_set<std::string_view>& batch_outputs,
  const std::unordered_set<std::string_view>& recursive_outputs
) {
  batch_inputs_ = batch_inputs;
  batch_outputs_ = batch_outputs;
  recursive_outputs_ = recursive_outputs;
}

void NodeBase::ProcessEvent() {
  auto event_ptr = std::dynamic_pointer_cast<OutputValue<Event>>(events_->ConsumeSingle(id_));
  if (event_ptr == nullptr) {
    throw std::runtime_error(id_ + ": cannot cast data for event " + id_);
  }

  const auto& event = event_ptr->value();

  bool should_process = (event.node_name.empty() || event.node_name == Id())
    && event.submodel_version_chain.IsPrefixOf(submodel_version_chain_);

  if (should_process) {
    if (!ProcessSystemEvent(event.event)) {
      ProcessEventImpl(event.event);
    }
  }

  next_seq_id_ = event_ptr->value_info().seq_id + 1;

  AfterAnyEvent();
}

bool NodeBase::ProcessSystemEvent(const std::any& event) {
  if (event.type() == typeid(ResetEvent)) {
    ProcessResetEvent(std::any_cast<const ResetEvent&>(event));
    return true;
  } else if (event.type() == typeid(SetActiveSubmodelEvent)) {
    ProcessSetActiveSubmodelEvent(std::any_cast<const SetActiveSubmodelEvent&>(event));
    return true;
  }
  return false;
}

void NodeBase::ProcessResetEvent(const ResetEvent& event) {
  output_batch_id_ = 0;
  Reset();
}

void NodeBase::ProcessSetActiveSubmodelEvent(const SetActiveSubmodelEvent& event) {
  auto new_status = [&]() -> std::optional<NodeStatus> {
    if (event.submodel_version_chain.ids_chain.size() > submodel_version_chain_.ids_chain.size()) {
      return std::nullopt;
    }

    for (int i = 0; i < event.submodel_version_chain.ids_chain.size(); ++i) {
      if (
        event.submodel_version_chain.ids_chain[i].id != submodel_version_chain_.ids_chain[i].id || (
          i + 1 < event.submodel_version_chain.ids_chain.size()
          && event.submodel_version_chain.ids_chain[i].version != submodel_version_chain_.ids_chain[i].version
        )
      ) {
        return std::nullopt;
      }
    }

    int n = event.submodel_version_chain.ids_chain.size() - 1;
    if (event.submodel_version_chain.ids_chain.size() == submodel_version_chain_.ids_chain.size()) {
      return static_cast<NodeStatus>(
        event.submodel_version_chain.ids_chain[n].version == submodel_version_chain_.ids_chain[n].version
      );
    } else if (event.submodel_version_chain.ids_chain[n].version != submodel_version_chain_.ids_chain[n].version) {
      return NodeStatus::INACTIVE;
    } else {
      return std::nullopt;
    }
  }();

  if (new_status.has_value() && new_status.value() != status_) {
    if (new_status.value() == NodeStatus::ACTIVE) {
      Reset();
    }

    status_ = new_status.value();
  }
}

} // namespace giga_edge
