#pragma once

#include "giga_edge/lib/environment/environment_base.h"
#include "giga_edge/lib/node/inputs_data.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/node_params.h"
#include "giga_edge/lib/node/outputs_data.h"
#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/statistics/statistics.h"
#include "giga_edge/lib/submodel_version/submodel_version.h"

#include <any>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace giga_edge {

struct ILoopScheduler;

class NodeBase;
using NodePtr = std::shared_ptr<NodeBase>;

struct NodeBaseConfig {
  // Common params set during creation in runtime.
  const SubmodelVersionChain submodel_version_chain;
  const NodeOutputPtr events;
  const std::function<void(NodePtr)> enqueue_callback;
  const std::function<void(NodePtr)> unqueue_callback;
  const CommonNodeParams& common_node_params;
  const uint32_t next_seq_id;
  const std::map<std::string, EnvironmentPtr>* environments;

  // Node specific params.
  NodeId base_id;
  std::unordered_map<std::string, NodeOutputPtr> inputs;
  std::unordered_map<std::string, NodeOutputPtr> outputs;
  int32_t priority;

  // Inputs, that may be in use before submodel creation,
  // thus we need to skip all data, present before start_batch_id.
  std::unordered_set<std::string> external_inputs;
};

enum class NextProcessingType {
  NONE = 0,
  INPUTS = 1,
  EVENT = 2,
  SKIP_INPUTS = 3,
  LOOP_ITERATION = 4,
  LOOP_INTERRUPT = 5,
};

enum class NodeStatus {
  INACTIVE = 0,
  ACTIVE = 1,
  REMOVED = 2,
};

class NodeBase : public NonCopyable, public std::enable_shared_from_this<NodeBase> {
public:
  explicit NodeBase(const NodeBaseConfig& config);

  // Should be called after constructor to subscribe for everything.
  void Start();
  bool IsStarted() const;

  const NodeId& Id() const;

  void Process();

  NextProcessingType GetNextProcessingType() const;

  virtual void Reset() = 0;

  void Enqueue();

  virtual void FillStatistics(SystemStatistics& statistics) const;

  virtual ~NodeBase() = default;

  void SetConnectionsInfo(
    const std::unordered_set<std::string_view>& batch_inputs,
    const std::unordered_set<std::string_view>& batch_outputs,
    const std::unordered_set<std::string_view>& recursive_outputs
  );

  virtual std::unordered_set<std::string> GetDynamicInputs() {
    return {};
  }
  virtual std::unordered_set<std::string> GetDynamicOutputs() {
    return {};
  }

  void InitRecursiveOutputs();

  bool CanPush() const;

  bool IsActive() const;
  NodeStatus Status() const;

  int32_t Priority() const;
  uint32_t NextSeqId() const;

  const std::unordered_map<std::string, NodeOutputPtr>& Inputs() const;
  const std::unordered_map<std::string, NodeOutputPtr>& Outputs() const;

  bool IsBatchInput(const std::string& input_id) const;
  bool IsBatchOutput(const std::string& output_id) const;
  bool IsRecursiveOutput(const std::string& output_id) const;

  void UnsubscribeExternalInputs(const std::unordered_set<NodeOutputPtr>& inputs);
  void SetRemoved();

  void SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler);

protected:
  // ProcessOnce should push one value / batch into each output for each consumed value / batch from input, or start
  // a loop.
  virtual void ProcessOnce() = 0;

  virtual void ProcessEventImpl([[maybe_unused]] const std::any& event) {};

  virtual void DoInitRecursiveOutputs();

  virtual int32_t GetMaxConsecutiveIterations() const {
    return IsInLoop() ? MAX_CONSEQUTIVE_LOOP_ITERATIONS : MAX_CONSEQUTIVE_ITERATIONS;
  }

  // Loop: if node calls StartLoop during inputs processing, it then will run inside loop.
  //   It will run ProcessLoopIterationImpl until it calls FinishLoop or new inputs / events appear.
  //   If new inputs / events appear before finishing loop, `ProcessLoopInterruptImpl` will be called.
  void StartLoop() {
    loop_status_ = LoopStatus::ACTIVE;
  }
  void FinishLoop() {
    loop_status_ = LoopStatus::INACTIVE;
    CloseOutputs();
  }
  bool IsInLoop() const {
    return loop_status_ == LoopStatus::ACTIVE;
  }

  virtual void ProcessLoopIterationImpl() {
    throw std::runtime_error("ProcessLoopIterationImpl is not implemented");
  };

  // Called after every consumed event, so that loop node could restart interrupted loop.
  virtual void AfterAnyEvent() {}

  // Loop interrupt should push data to all non-closed batch outputs.
  virtual void ProcessLoopInterruptImpl() {
    throw std::runtime_error("ProcessLoopInterruptImpl is not implemented");
  };

private:
  void ProcessActive();
  void ProcessInactive();

  void ProcessInputs();

  void ConsumeInputs();
  void InitOutputsData();
  void CloseOutputs();
  void PushOutputs();

  void AddTimer(int32_t id);
  void TriggerTimer(int32_t id);

  void ProcessSkipInactiveInputs();

  void ProcessEvent();
  bool ProcessSystemEvent(const std::any& event);
  void ProcessResetEvent(const ResetEvent& event);
  void ProcessSetActiveSubmodelEvent(const SetActiveSubmodelEvent& event);

  NextProcessingType GetNextProcessingTypeActive() const;
  NextProcessingType GetNextProcessingTypeInactive() const;

  void ProcessLoopIteration();
  void ProcessLoopInterrupt();

protected:
  NodeId id_;

  std::unique_ptr<InputsData> inputs_data_;
  std::unique_ptr<OutputsData> outputs_data_;

  int32_t priority_;

private:
  std::unordered_map<std::string, NodeOutputPtr> inputs_;
  std::unordered_map<std::string, NodeOutputPtr> outputs_;

  std::unordered_set<std::string_view> batch_inputs_;
  std::unordered_set<std::string_view> batch_outputs_;
  std::unordered_set<std::string_view> recursive_outputs_;

  NodeOutputPtr events_;

  uint32_t next_seq_id_;

  uint32_t output_batch_id_;

  static const int32_t MAX_CONSEQUTIVE_ITERATIONS = 10;
  static const int32_t MAX_CONSEQUTIVE_LOOP_ITERATIONS = 3;
  static const int32_t MAX_OUTPUT_SIZE = 10;

  std::function<void(NodePtr)> enqueue_callback_;
  std::function<void(NodePtr)> unqueue_callback_;

  std::unique_ptr<BatchStatistics> statistics_;

  std::unordered_set<std::string> external_inputs_;

  SubmodelVersionChain submodel_version_chain_;
  NodeStatus status_;
  bool is_started_;

  enum class LoopStatus {
    INACTIVE, // the node is not in a loop, waits for data
    ACTIVE,   // the node is in the generation loop
  } loop_status_ = LoopStatus::INACTIVE;

  std::shared_ptr<ILoopScheduler> loop_scheduler_;
};

} // namespace giga_edge
