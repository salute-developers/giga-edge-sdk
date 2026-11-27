#pragma once

#include "statistics_tracker.h"
#include "submodel.h"

#include "giga_edge/lib/common/non_copyable.h"
#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/environment/environment_base.h"
#include "giga_edge/lib/node/event.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/output_value.h"
#include "giga_edge/lib/node/types.h"

#include <cassert>
#include <stdexcept>
#include <unordered_map>

namespace giga_edge {

class IThreadPool;
struct RuntimeParams;
class SequenceIdGenerator;
class NodeBase;
using NodePtr = std::shared_ptr<NodeBase>;
struct ILoopScheduler;

struct EventCacheAction {
  enum class Action { SET, CLEAN } action;

  std::string key;
};

class Runtime : public NonCopyable {
public:
  explicit Runtime(
    const std::string& path, const std::string& config_path = "config.yaml", EnvironmentsMap environments = {}
  );

  explicit Runtime(
    const IDataReaderPtr& data_reader, const std::string& config_path = "config.yaml", EnvironmentsMap environments = {}
  );

  Runtime(const IDataReaderPtr& data_reader, RuntimeParams params, EnvironmentsMap environments = {});

  ~Runtime();

  const std::vector<std::string>& InputNames() const;
  bool HasInput(const std::string& input_name) const;
  void SetInputs(const std::vector<std::string>& input_names);
  void SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler);

  bool HasOutput(const SubmodelVersionChain& version_chain, const std::string& node_output_id);

  const std::string& Version() const;
  bool Empty() const;

  void Reset();
  void Reset(const SubmodelVersionChain& version_chain);

  RuntimeSubmodelVersionPtr Core() const;
  int LoadThreads() const;

  void PushEvent(Event event, std::optional<EventCacheAction> event_cache_action = std::nullopt);

  // Version management may be called from any thread and does not stop the graph.
  // Activations are ordered by the event barriers of the thread pool: each call adds a barrier and commits it when
  // every node has consumed the activation, and the pool passes the barriers in the order they were added.
  // RemoveSubmodelVersion returns when the nodes of the version are inactive and idle: for a version that is still
  // active it waits until another version of the submodel is activated.
  void AddSubmodelVersion(
    const SubmodelVersionChain& version_chain,
    const IDataReaderPtr& data_reader,
    const std::string& config_path = "config.yaml"
  );
  void SetActiveSubmodelVersion(const SubmodelVersionChain& version_chain);
  void SetActiveSubmodelVersions(const std::vector<SubmodelVersionChain>& version_chains);
  void RemoveSubmodelVersion(const SubmodelVersionChain& version_chain);
  std::unordered_map<std::string, std::string> CollectConsumedVersions() const;

  SystemStatistics GetStatistics() const;

  void WriteDot(const std::string& filepath) const;
  EnvironmentPtr GetEnvironment(const std::string& name) const;

  template<class T>
  void Push(
    const SubmodelVersionChain& version_chain, const std::string& node_output_id, T data, const std::any& external_data
  ) {
    auto seq_id = GenerateNextInputId(node_output_id);
    PushImpl(
      version_chain,
      node_output_id,
      std::move(data),
      {
        .batch_infos = {{.id = seq_id, .is_last = true}},
        .seq_id = seq_id,
        .external_data = external_data,
      }
    );
  }

  template<class T>
  void PushValueInfo(
    const SubmodelVersionChain& version_chain, const std::string& node_output_id, T data, const ValueInfo& value_info
  ) {
    PushImpl(version_chain, node_output_id, std::move(data), value_info);
  }

  template<class T>
  NodeSubscriptionId Subscribe(
    const SubmodelVersionChain& version_chain,
    const std::string& output_id,
    std::function<void(const T&, const std::any&)> callback
  ) {
    return SubscribeImpl<T>(
      version_chain, output_id, [callback = std::move(callback)](const T& item, const ValueInfo& value_info) {
        callback(item, value_info.external_data);
      }
    );
  }

  template<class T>
  NodeSubscriptionId SubscribeValueInfo(
    const SubmodelVersionChain& version_chain,
    const std::string& output_id,
    std::function<void(const T&, const ValueInfo&)> callback
  ) {
    return SubscribeImpl<T>(
      version_chain, output_id, [callback = std::move(callback)](const T& item, const ValueInfo& value_info) {
        callback(item, value_info);
      }
    );
  }

  void Unsubscribe(const NodeSubscriptionId& subscriber_id);

private:
  template<class T>
  void PushImpl(
    const SubmodelVersionChain& version_chain, const std::string& node_output_id, T data, ValueInfo value_info
  ) {
    statistics_tracker_.StartTimer(value_info.seq_id);
    FindSubmodelVersion(version_chain)->NodeOutput(node_output_id)->Push<T>(std::move(data), std::move(value_info));
  }

  template<class T>
  NodeSubscriptionId SubscribeImpl(
    const SubmodelVersionChain& version_chain,
    const std::string& output_id,
    std::function<void(const T&, const ValueInfo&)> callback
  ) {
    std::string subscriber_id;
    {
      std::lock_guard lock(subscribers_mutex_);
      subscriber_id = "graph_output_" + std::to_string(++next_subscriber_id_);
    }

    auto output = FindSubmodelVersion(version_chain)->NodeOutput(output_id);
    output->Subscribe(
      subscriber_id,
      [this, subscriber_id, callback = std::move(callback), weak_output = std::weak_ptr<NodeOutput>(output)]() {
        auto output = weak_output.lock();
        if (output == nullptr) {
          throw std::logic_error("Node output is missing while in callback");
        }

        OutputValueBasePtr raw_output;
        try {
          raw_output = output->ConsumeSingle(subscriber_id);
        } catch (const std::out_of_range&) {
          // Subscription can be removed concurrently with callback dispatch.
          return;
        }

        auto result = std::dynamic_pointer_cast<OutputValue<T>>(raw_output);
        if (result == nullptr) {
          throw std::runtime_error("Cannot cast data for output: " + output->Id());
        }
        statistics_tracker_.TriggerTimer(result->value_info().seq_id, subscriber_id);
        callback(result->value(), result->value_info());
      }
    );

    {
      std::lock_guard lock(subscribers_mutex_);
      subscriber_outputs_[subscriber_id] = output;
    }
    statistics_tracker_.AddSubscriber(subscriber_id);

    return subscriber_id;
  }

private:
  void Enqueue(NodePtr node);
  void Unqueue(NodePtr node);

  RuntimeSubmodelVersionPtr FindSubmodelVersion(const SubmodelVersionChain& version_chain);

  uint32_t NextSeqId() const;
  uint32_t GenerateNextInputId(const NodeOutputId& output_id);

  uint32_t GenerateNextEventIdLocked(bool with_barrier = false);
  std::vector<uint32_t> GenerateNextEventIdsLocked(int n, bool with_barrier = false);

private:
  const std::unique_ptr<const RuntimeParams> params_;

  RuntimeSubmodelVersionPtr core_submodel_;
  std::shared_ptr<ILoopScheduler> loop_scheduler_;

  std::vector<std::string> input_names_;

  int32_t next_subscriber_id_;
  std::mutex subscribers_mutex_;
  std::unordered_map<NodeId, std::weak_ptr<NodeOutput>> subscriber_outputs_;
  StatisticsTracker statistics_tracker_;

  mutable std::mutex seq_mutex_;
  std::unique_ptr<SequenceIdGenerator> id_generator_;

  NodeOutputPtr events_input_;

  mutable std::recursive_mutex mutex_;
  std::unordered_set<NodeId> nodes_in_queue_;
  std::condition_variable_any cv_;
  // The active version of every submodel with the seq_id of its activation: activations may finish out of order.
  std::vector<std::pair<SubmodelVersionChain, uint32_t>> active_version_chains_;

  std::unique_ptr<IThreadPool> thread_pool_;

  std::unordered_map<std::string, Event> cached_events_;
  EnvironmentsMap environments_;
};

} // namespace giga_edge
