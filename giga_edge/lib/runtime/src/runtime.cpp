#include "giga_edge/lib/runtime/runtime.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/params.h"
#include "giga_edge/lib/sequence_id_generator/sequence_id_generator.h"
#include "giga_edge/lib/version/version.h"

#include <algorithm>
#include <fstream>

namespace giga_edge {
namespace {

auto LoadYaml(const IDataReaderPtr& data_reader, const std::string& path) {
  auto span = data_reader->ReadTransientData(path);
  auto [data, size] = EnsureNotNull(span, path)->Get();
  return ParseYaml(data, size);
}

EnvironmentsMap BuildEnvironments(
  EnvironmentsMap parent_environments, const EnvironmentsMapParams& new_environments_params
) {
  for (const auto& [env_name, env_params] : new_environments_params) {
    parent_environments[env_name] = MakeEnvironment(env_params);
  }
  return parent_environments;
}

std::unordered_map<NodeOutputId, NodeOutputPtr> GetInputs(const RuntimeParams& params) {
  std::unordered_map<std::string, bool> is_input;
  for (const auto& node : params.nodes) {
    for (const auto& [_, input] : node.inputs) {
      is_input.try_emplace(input, true);
    }
    for (const auto& [_, output] : node.outputs) {
      is_input[output] = false;
    }
  }

  for (const auto& submodel : params.submodels) {
    for (const auto& [_, input] : submodel.inputs) {
      is_input.try_emplace(input, true);
    }
    for (const auto& [_, output] : submodel.outputs) {
      is_input[output] = false;
    }
  }

  std::unordered_map<NodeOutputId, NodeOutputPtr> result;
  for (const auto& [input_id, is_input_flag] : is_input) {
    if (is_input_flag) {
      result[input_id] = std::make_shared<NodeOutput>(input_id);
    }
  }

  return result;
}

SubmodelVersionChain MakeVersionSlotKey(SubmodelVersionChain version_chain) {
  version_chain.ids_chain.back().version.clear();
  return version_chain;
}

// The latest activation of a submodel wins, whichever call gets here first.
void UpsertActiveVersionChain(
  std::vector<std::pair<SubmodelVersionChain, uint32_t>>& active_version_chains,
  const SubmodelVersionChain& version_chain,
  uint32_t seq_id
) {
  const auto version_chain_key = MakeVersionSlotKey(version_chain);
  for (auto it = active_version_chains.begin(); it != active_version_chains.end(); ++it) {
    if (MakeVersionSlotKey(it->first) == version_chain_key) {
      if (static_cast<int32_t>(seq_id - it->second) < 0) {
        return;
      }
      active_version_chains.erase(it);
      break;
    }
  }
  active_version_chains.emplace_back(version_chain, seq_id);
}

} // namespace

Runtime::Runtime(const std::string& path, const std::string& config_path, EnvironmentsMap environments)
  : Runtime(CreateDataReader(path), config_path, std::move(environments)) {}

Runtime::Runtime(const IDataReaderPtr& data_reader, const std::string& config_path, EnvironmentsMap environments)
  : Runtime(data_reader, ParseConfig<RuntimeParams>(LoadYaml(data_reader, config_path)), std::move(environments)) {}

Runtime::Runtime(const IDataReaderPtr& data_reader, RuntimeParams params, EnvironmentsMap environments)
  : params_(std::make_unique<const RuntimeParams>(std::move(params)))
  , next_subscriber_id_(0)
  , statistics_tracker_(
      params_->common_node_params.statistics_length
        ? std::make_unique<BatchStatistics>("runtime", params_->common_node_params.statistics_length)
        : nullptr,
      params_->statistics_every_batches,
      [this]() {
        GetStatistics().Print(params_->version);
      }
    )
  , events_input_(std::make_shared<NodeOutput>("events")) {
  environments_ = BuildEnvironments(std::move(environments), params_->environments_map_params);

  CheckIfCompatible(params_->lib_version);

  if (params_->num_threads == 0) {
    thread_pool_ = std::make_unique<DummyThreadPool>();
  } else {
    thread_pool_ = std::make_unique<ThreadPool>(
      params_->num_threads,
      params_->realtime_priority,
      params_->num_realtime_threads,
      params_->thread_name_prefix,
      params_->version
    );
  }

  auto inputs = GetInputs(*params_);
  input_names_.reserve(inputs.size());
  for (const auto& [input, _] : inputs) {
    input_names_.push_back(input);
  }
  assert(!input_names_.empty());

  id_generator_ = std::make_unique<SequenceIdGenerator>(input_names_);

  NodeBaseConfig node_base_config{
    .submodel_version_chain = {},
    .events = events_input_,
    .enqueue_callback =
      [this](NodePtr node) {
        Enqueue(node);
      },
    .unqueue_callback =
      [this](NodePtr node) {
        Unqueue(node);
      },
    .common_node_params = params_->common_node_params,
    .next_seq_id = id_generator_->NextSeqId(),
    .environments = &environments_
  };

  auto core_submodel_params = static_cast<SubmodelParams>(*params_);
  core_submodel_params.environments_map_params.clear();

  core_submodel_ = std::make_shared<RuntimeSubmodelVersion>(
    node_base_config,
    inputs,
    std::unordered_map<NodeOutputId, NodeOutputPtr>{},
    std::move(core_submodel_params),
    data_reader
  );
  core_submodel_->Start();
}

Runtime::~Runtime() {
  thread_pool_->Stop();
}

void Runtime::SetInputs(const std::vector<std::string>& input_names) {
  input_names_ = input_names;
  id_generator_ = std::make_unique<SequenceIdGenerator>(input_names, id_generator_->NextSeqId());
}

void Runtime::SetLoopScheduler(const std::shared_ptr<ILoopScheduler>& scheduler) {
  loop_scheduler_ = scheduler;
  if (core_submodel_) {
    core_submodel_->SetLoopScheduler(scheduler);
  }
}

bool Runtime::HasOutput(const SubmodelVersionChain& version_chain, const std::string& output_id) {
  try {
    return FindSubmodelVersion(version_chain)->HasOutput(output_id);
  } catch (const std::runtime_error&) {
    return false;
  }
}

RuntimeSubmodelVersionPtr Runtime::FindSubmodelVersion(const SubmodelVersionChain& version_chain) {
  // Hot path: an empty chain does not walk the version tree, the core itself is never added or removed.
  if (version_chain.ids_chain.empty()) {
    return core_submodel_;
  }
  std::lock_guard lock(mutex_);
  return core_submodel_->SubmodelVersion(version_chain);
}

RuntimeSubmodelVersionPtr Runtime::Core() const {
  return core_submodel_;
}

int Runtime::LoadThreads() const {
  return params_->load_threads;
}

void Runtime::AddSubmodelVersion(
  const SubmodelVersionChain& version_chain, const IDataReaderPtr& data_reader, const std::string& config_path
) {
  if (version_chain.ids_chain.empty()) {
    throw std::invalid_argument("Cannot add empty submodel version");
  }

  auto next_seq_id = NextSeqId();
  const auto submodel_params = ParseConfig<SubmodelParams>(LoadYaml(data_reader, config_path));
  // Unlike the top-level config, a version may leave lib_version out; a stated one is checked the same way.
  if (!submodel_params.lib_version.empty()) {
    CheckIfCompatible(submodel_params.lib_version);
  }

  RuntimeSubmodelPtr submodel;
  // The version that the new one is nested in: the new version sees its environments. Held until the end of the
  // call, so that they outlive a removal of the parent.
  RuntimeSubmodelVersionPtr parent_version;
  {
    std::lock_guard lock(mutex_);
    submodel = core_submodel_->Submodel(version_chain);
    parent_version = core_submodel_->SubmodelVersion(version_chain.PopBack());
  }

  NodeBaseConfig node_base_config{
    .submodel_version_chain = version_chain,
    .events = events_input_,
    .enqueue_callback =
      [this](NodePtr node) {
        Enqueue(node);
      },
    .unqueue_callback =
      [this](NodePtr node) {
        Unqueue(node);
      },
    .common_node_params = params_->common_node_params,
    .next_seq_id = next_seq_id,
    .environments = &parent_version->Environments()
  };

  auto submodel_version = std::make_shared<RuntimeSubmodelVersion>(
    node_base_config, submodel->Inputs(), submodel->Outputs(), submodel_params, data_reader
  );

  if (loop_scheduler_) {
    submodel_version->SetLoopScheduler(loop_scheduler_);
  }

  std::lock_guard lock(mutex_);
  submodel->AddVersion(version_chain.ids_chain.back().version, std::move(submodel_version));
}

void Runtime::SetActiveSubmodelVersion(const SubmodelVersionChain& version_chain) {
  SetActiveSubmodelVersions({version_chain});
}

void Runtime::SetActiveSubmodelVersions(const std::vector<SubmodelVersionChain>& version_chains) {
  if (version_chains.empty()) {
    return;
  }
  for (const auto& version_chain : version_chains) {
    if (version_chain.ids_chain.empty()) {
      throw std::invalid_argument("Cannot set empty submodel version");
    }
  }

  {
    std::lock_guard lock(mutex_);
    for (const auto& version_chain : version_chains) {
      auto submodel_version = core_submodel_->SubmodelVersion(version_chain);
      submodel_version->Start();
    }
  }

  uint32_t push_id = 0;
  uint32_t barrier_seq_id = 0;
  {
    std::lock_guard lock(seq_mutex_);
    auto seq_ids = GenerateNextEventIdsLocked(version_chains.size() + cached_events_.size(), true);
    barrier_seq_id = seq_ids.back();
    int32_t i = 0;
    for (const auto& version_chain : version_chains) {
      push_id = events_input_->Push(
        Event{
          .event = SetActiveSubmodelEvent{
            .submodel_version_chain = version_chain,
          },
        },
        {.seq_id = seq_ids[i++]}
      );
    }
    for (const auto& [_, event] : cached_events_) {
      push_id = events_input_->Push(event, {.seq_id = seq_ids[i++]});
    }
  }
  events_input_->WaitConsumedTo(push_id);
  // Another activation may be in flight: each one commits its own barrier.
  thread_pool_->CommitEventBarrier(barrier_seq_id);

  {
    std::lock_guard lock(mutex_);
    for (const auto& version_chain : version_chains) {
      UpsertActiveVersionChain(active_version_chains_, version_chain, barrier_seq_id);
    }
  }
}

void Runtime::RemoveSubmodelVersion(const SubmodelVersionChain& version_chain) {
  if (version_chain.ids_chain.empty()) {
    throw std::invalid_argument("Cannot remove empty submodel version");
  }
  RuntimeSubmodelPtr submodel;
  std::vector<NodePtr> nodes;
  {
    // The version tree is read under the lock: versions may be added to it at the same time.
    std::lock_guard lock(mutex_);
    submodel = core_submodel_->Submodel(version_chain);
    nodes = submodel->VersionNodes(version_chain.ids_chain.back().version);
  }
  // Outside the lock, so that the graph keeps running: each node waits until it is inactive and idle.
  submodel->DetachNodes(nodes);
  {
    std::lock_guard lock(mutex_);
    submodel->RemoveVersion(version_chain.ids_chain.back().version);
    active_version_chains_.erase(
      std::remove_if(
        active_version_chains_.begin(),
        active_version_chains_.end(),
        [&version_chain](const auto& active_version_chain) {
          return version_chain.IsPrefixOf(active_version_chain.first);
        }
      ),
      active_version_chains_.end()
    );
  }
}

std::unordered_map<std::string, std::string> Runtime::CollectConsumedVersions() const {
  std::lock_guard lock(mutex_);
  std::unordered_map<std::string, std::string> consumed_versions;
  for (const auto& [active_version_chain, _] : active_version_chains_) {
    consumed_versions[MakeVersionSlotKey(active_version_chain).AsString()]
      = active_version_chain.ids_chain.back().version;
  }
  return consumed_versions;
}

void Runtime::Unsubscribe(const NodeSubscriptionId& subscriber_id) {
  NodeOutputPtr output;
  {
    std::lock_guard lock(subscribers_mutex_);
    auto subscriber_it = subscriber_outputs_.find(subscriber_id);
    if (subscriber_it == subscriber_outputs_.end()) {
      return;
    }

    output = subscriber_it->second.lock();
    subscriber_outputs_.erase(subscriber_it);
  }
  statistics_tracker_.RemoveSubscriber(subscriber_id);

  if (output != nullptr) {
    output->Unsubscribe(subscriber_id);
  }
}

const std::vector<std::string>& Runtime::InputNames() const {
  return input_names_;
}

bool Runtime::HasInput(const std::string& input_name) const {
  return std::find(input_names_.begin(), input_names_.end(), input_name) != input_names_.end();
}

void Runtime::Enqueue(NodePtr node) {
  std::lock_guard lock(mutex_);
  if (nodes_in_queue_.find(node->Id()) != nodes_in_queue_.end()) {
    return;
  }

  if (node->Status() == NodeStatus::REMOVED) {
    return;
  }

  if (node->GetNextProcessingType() == NextProcessingType::NONE) {
    return;
  }
  if (nodes_in_queue_.emplace(node->Id()).second) {
    thread_pool_->Push(
      std::make_shared<Task>(
        [node, &mutex = mutex_, &cv = cv_, &nodes_in_queue = nodes_in_queue_]() {
          {
            std::lock_guard lock(mutex);
            if (node->Status() == NodeStatus::REMOVED) {
              nodes_in_queue.erase(node->Id());
              cv.notify_all();
              return;
            }
            assert(nodes_in_queue.find(node->Id()) != nodes_in_queue.end());
          }
          node->Process();
          {
            std::lock_guard lock(mutex);
            nodes_in_queue.erase(node->Id());
          }
          cv.notify_all();
          node->Enqueue();
        },
        node->Priority(),
        node->NextSeqId(),
        node->GetNextProcessingType() == NextProcessingType::INPUTS ? std::optional<uint32_t>(node->NextSeqId())
                                                                    : std::nullopt
      )
    );
  }
}

void Runtime::Unqueue(NodePtr node) {
  std::unique_lock lock(mutex_);

  if (node->IsStarted()) [[likely]] {
    cv_.wait(lock, [&node, &nodes_in_queue = nodes_in_queue_]() {
      if (nodes_in_queue.find(node->Id()) != nodes_in_queue.end()) {
        return false;
      }

      auto processing_type = node->GetNextProcessingType();
      bool need_to_process = processing_type == NextProcessingType::EVENT
        || processing_type == NextProcessingType::INPUTS;

      return !node->IsActive() && !need_to_process;
    });
  }

  node->SetRemoved();
}

SystemStatistics Runtime::GetStatistics() const {
  // mutex_ guards only the graph structure: the statistics themselves are computed outside the lock,
  // otherwise it would stall the scheduler for the whole time the samples are sorted.
  std::vector<NodePtr> nodes;
  {
    std::lock_guard lock(mutex_);
    core_submodel_->CollectNodes(nodes);
  }

  SystemStatistics result;
  statistics_tracker_.Fill(result);
  for (const auto& node : nodes) {
    node->FillStatistics(result);
  }

  return result;
}

bool Runtime::Empty() const {
  std::lock_guard lock(mutex_);
  if (!nodes_in_queue_.empty()) {
    return false;
  }

  return core_submodel_->Empty();
}

const std::string& Runtime::Version() const {
  return params_->version;
}

void Runtime::Reset(const SubmodelVersionChain& version_chain) {
  PushEvent(
    Event{
      .event = ResetEvent{},
      .submodel_version_chain = version_chain,
    }
  );
}

void Runtime::Reset() {
  PushEvent(
    Event{
      .event = ResetEvent{},
    }
  );
}

void Runtime::PushEvent(Event event, std::optional<EventCacheAction> event_cache_action) {
  std::lock_guard lock(seq_mutex_);

  if (event_cache_action.has_value()) {
    switch (event_cache_action->action) {
      case EventCacheAction::Action::SET:
        cached_events_[event_cache_action->key] = event;
        break;
      case EventCacheAction::Action::CLEAN:
        cached_events_.erase(event_cache_action->key);
        break;
    }
  }

  events_input_->Push(event, {.seq_id = GenerateNextEventIdLocked()});
}

void Runtime::WriteDot(const std::string& filepath) const {
  std::ofstream out(filepath);
  std::stringstream edges_out;
  std::unordered_map<NodeOutputPtr, std::string> node_output_names;

  out << "digraph " << "{" << std::endl;
  out << "  label = \"" << Version() << "\"" << std::endl;

  core_submodel_->WriteDot(out, edges_out, node_output_names);
  out << edges_out.str();
  out << "}";
}

EnvironmentPtr Runtime::GetEnvironment(const std::string& name) const {
  auto it = environments_.find(name);
  if (it == environments_.end()) {
    return nullptr;
  }
  return it->second;
}

uint32_t Runtime::NextSeqId() const {
  std::lock_guard lock(seq_mutex_);
  return id_generator_->NextSeqId();
}

uint32_t Runtime::GenerateNextInputId(const NodeOutputId& output_id) {
  std::lock_guard lock(seq_mutex_);
  return id_generator_->GenerateNextInputId(output_id);
}

uint32_t Runtime::GenerateNextEventIdLocked(bool with_barrier) {
  auto seq_id = id_generator_->GenerateNextEventId();
  if (with_barrier) {
    thread_pool_->AddEventBarrier(seq_id);
  }
  return seq_id;
}

std::vector<uint32_t> Runtime::GenerateNextEventIdsLocked(int n, bool with_barrier) {
  std::vector<uint32_t> result;
  result.reserve(n);
  for (int i = 0; i < n; ++i) {
    result.push_back(id_generator_->GenerateNextEventId());
  }
  if (with_barrier) {
    thread_pool_->AddEventBarrier(result.back());
  }
  return result;
}

} // namespace giga_edge
