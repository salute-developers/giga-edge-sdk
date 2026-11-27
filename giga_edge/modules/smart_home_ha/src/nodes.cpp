#include "giga_edge/modules/smart_home_ha/nodes.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/mcp/nodes/node_coordinator.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
namespace giga_edge {
template<>
ha::CatalogParams ParseConfig(const YAML::Node& config) {
  ha::CatalogParams p;
  p.environment_name = config["environment_name"].as<std::string>(p.environment_name);
  p.request_timeout_ms = config["request_timeout_ms"].as<uint32_t>(p.request_timeout_ms);
  if (p.environment_name.empty() || !p.request_timeout_ms) {
    throw std::invalid_argument("Invalid HA MCP configuration");
  }
  p.resource_uri = config["resource_uri"].as<std::string>(p.resource_uri);
  p.server_ids = config["server_ids"].as<std::vector<std::string>>(p.server_ids);
  p.max_context_bytes = config["max_context_bytes"].as<size_t>(p.max_context_bytes);
  p.refresh_interval_ms = config["refresh_interval_ms"].as<uint32_t>(p.refresh_interval_ms);
  p.context_wait_ms = config["context_wait_ms"].as<uint32_t>(p.context_wait_ms);
  p.context_retry_ms = config["context_retry_ms"].as<uint32_t>(p.context_retry_ms);
  if (!p.context_retry_ms) {
    throw std::invalid_argument("HA catalog context_retry_ms must be positive");
  }
  p.current_room = config["current_room"].as<std::string>(p.current_room);
  p.include_online = config["include_online"].as<bool>(p.include_online);
  if (p.resource_uri.empty() || !p.max_context_bytes) {
    throw std::invalid_argument("Invalid HA catalog limits");
  }
  return p;
}
template<>
ha::SceneParams ParseConfig(const YAML::Node& config) {
  ha::SceneParams p;
  p.max_chars = config["max_chars"].as<size_t>(p.max_chars);
  p.scenario_prefix = config["scenario_prefix"].as<std::string>(p.scenario_prefix);
  if (!p.max_chars) {
    throw std::invalid_argument("HA scene max_chars must be positive");
  }
  return p;
}

HaCatalogNode::HaCatalogNode(const NodeBaseConfig& base, const ha::CatalogParams& params)
  : NodeBase(base)
  , params_(params) {
  // Without a coordinator the node still serves the hydrated catalog, just never reads the context itself.
  std::string error;
  coordinator_ = mcp::nodes::ResolveCoordinator(base, params_.environment_name, error);
  if (coordinator_) {
    mcp::McpConsumerConfig config;
    // Register (not EnsureConsumer): refresh completions must never be consumed by the
    // action executor/pickup pair. An empty name keeps EnsureConsumer from attaching here.
    config.visible_server_ids = {params_.server_ids.begin(), params_.server_ids.end()};
    config.completion_queue_capacity = 8;
    consumer_id_ = coordinator_->RegisterConsumer(std::move(config));
  }
}
HaCatalogNode::~HaCatalogNode() {
  if (coordinator_ && consumer_id_) {
    coordinator_->UnregisterConsumer(consumer_id_);
  }
}
void HaCatalogNode::CancelReads() {
  for (const auto& [token, index] : reads_) {
    coordinator_->CancelRequest(consumer_id_, token);
  }
  reads_.clear();
}
void HaCatalogNode::Reset() {
  CancelReads();
  cached_.reset();
}
bool HaCatalogNode::RefreshDue() const {
  if (!consumer_id_) {
    return false;
  }
  return params_.refresh_interval_ms
    && std::chrono::steady_clock::now() - refreshed_at_ >= std::chrono::milliseconds(params_.refresh_interval_ms);
}
bool HaCatalogNode::UnreadDue() const {
  const bool unread = std::any_of(
    native_.latest_resource_states.begin(), native_.latest_resource_states.end(), [](const auto& state) {
      return state.data_json.empty();
    }
  );
  return consumer_id_ && unread
    && std::chrono::steady_clock::now() - read_at_ >= std::chrono::milliseconds(params_.context_retry_ms);
}
void HaCatalogNode::ProcessOnce() {
  const auto& s = GetSnapshot();
  if (
    !cached_ || s.revision.empty() || cached_->revision != s.revision || input_status_ != s.status
    || input_error_ != s.error
  ) {
    Reset();
    native_ = s;
    input_status_ = s.status;
    input_error_ = s.error;
    refreshed_at_ = std::chrono::steady_clock::now();
    if (AddUnreadContext()) {
      StartNativeRefresh(false);
    }
    cached_ = builder_.Build(ReadContext(), params_);
    WaitForContext();
  } else if (reads_.empty()) {
    if (const bool all = RefreshDue(); all || UnreadDue()) {
      StartNativeRefresh(all);
    }
  }
  PollNativeRefresh();
  // A typo in the room of the assistant would silently give the model a room that does not exist: warn once.
  if (!room_checked_ && !params_.current_room.empty() && cached_ && !cached_->devices.empty()) {
    room_checked_ = true;
    const bool known = std::any_of(cached_->devices.begin(), cached_->devices.end(), [this](const auto& device) {
      const auto& areas = device.second.areas;
      return std::find(areas.begin(), areas.end(), params_.current_room) != areas.end();
    });
    if (!known) {
      std::cerr << "[ha_catalog] current_room '" << params_.current_room << "' is not an area of the house"
                << std::endl;
    }
  }
  PushCatalog(cached_);
  if (HasCurrentRoomOutput()) {
    PushCurrentRoom(params_.current_room);
  }
}
// The server has listed the context resource, but MCP has not read it yet (a request right after connecting): an
// empty state is a "to be read" mark; the node reads it itself and retries until the read succeeds.
bool HaCatalogNode::AddUnreadContext() {
  if (!consumer_id_) {
    return false;
  }
  auto& states = native_.latest_resource_states;
  bool added = false;
  for (const auto& resource : native_.resources) {
    const bool listed = std::any_of(states.begin(), states.end(), [&](const auto& state) {
      return state.server_id == resource.server_id && state.uri == resource.uri;
    });
    const auto& visible = params_.server_ids;
    const bool own = visible.empty() || std::find(visible.begin(), visible.end(), resource.server_id) != visible.end();
    if (own && resource.uri == params_.resource_uri && !listed) {
      states.push_back({.server_id = resource.server_id, .uri = resource.uri, .data_json = {}});
      added = true;
    }
  }
  return added;
}
// The house is empty while the context is being read: the tick waits for it at most context_wait_ms, otherwise the
// turn would end with a "no devices" error; the read is not cancelled at the deadline: PollNativeRefresh finishes
// it on later ticks.
void HaCatalogNode::WaitForContext() {
  if (reads_.empty() || !cached_->devices.empty() || !params_.context_wait_ms) {
    return;
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(params_.context_wait_ms);
  while (!reads_.empty() && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    PollNativeRefresh();
  }
}
McpCatalogSnapshot HaCatalogNode::ReadContext() const {
  auto snapshot = native_;
  auto& states = snapshot.latest_resource_states;
  states.erase(
    std::remove_if(
      states.begin(),
      states.end(),
      [](const auto& state) {
        return state.data_json.empty();
      }
    ),
    states.end()
  );
  return snapshot;
}
void HaCatalogNode::StartNativeRefresh(bool all) {
  read_at_ = std::chrono::steady_clock::now();
  if (all) {
    refreshed_at_ = read_at_;
  }
  refresh_changed_ = false;
  for (size_t i = 0; i < native_.latest_resource_states.size(); ++i) {
    const auto& state = native_.latest_resource_states[i];
    if (state.uri != params_.resource_uri || (!all && !state.data_json.empty())) {
      continue;
    }
    const auto submitted = coordinator_->SubmitReadResource(
      consumer_id_,
      {.call_id = "ha-catalog/refresh/" + std::to_string(++request_index_),
       .server_id = state.server_id,
       .uri = state.uri,
       .timeout_ms = params_.request_timeout_ms}
    );
    if (submitted.accepted) {
      reads_.emplace(submitted.token, i);
    }
  }
}
void HaCatalogNode::PollNativeRefresh() {
  if (reads_.empty()) {
    return;
  }
  auto results = coordinator_->TakeReadResourceResults(consumer_id_, 8);
  const bool failed = results.overflow_happened;
  for (const auto& completion : results.items) {
    const auto it = reads_.find(completion.token);
    if (it == reads_.end()) {
      continue;
    }
    if (completion.result.is_error) {
      // A failure of one server does not affect the others: its previous data stays, and UnreadDue retries the
      // unread context.
      reads_.erase(it);
      continue;
    }
    // Same shape the MCP service stores for resources it reads on list.
    Json::Value state;
    state["uri"] = completion.result.uri;
    state["mimeType"] = completion.result.mime_type;
    state["text"] = completion.result.text;
    auto data_json = WriteJson(state);
    auto& stored = native_.latest_resource_states[it->second].data_json;
    refresh_changed_ |= data_json != stored;
    stored = std::move(data_json);
    reads_.erase(it);
  }
  if (failed) {
    // Queue overflow: some completions are lost, so the reads are cancelled and the previous catalog is kept; what
    // has already been stored gets into it on the next context change.
    CancelReads();
    return;
  }
  // An unchanged context keeps the catalog (and the rendered scene) as is.
  if (!reads_.empty() || !refresh_changed_) {
    return;
  }
  auto built = builder_.Build(ReadContext(), params_);
  if (built->status == AssistantStatus::OK || !cached_ || cached_->status != AssistantStatus::OK) {
    cached_ = std::move(built);
  }
}
void HaSceneNode::ProcessOnce() {
  const auto& c = GetCatalog();
  if (!c) {
    PushScene(SceneSnapshot{.status = AssistantStatus::ERROR, .error = "Missing HA catalog"});
    return;
  }
  if (c != catalog_) {
    catalog_ = c;
    scene_ = ha::RenderScene(*c, params_);
  }
  PushScene(scene_);
}
void HaActionResolverNode::ProcessOnce() {
  const auto& query = GetQuery();
  const bool reset = GetResetFlag();
  if (reset || !query.text.empty()) {
    Reset();
    if (!query.text.empty()) {
      expected_generation_ = query.request_id;
    }
  }
  if (reset) {
    PushAction(McpResolvedAction{.status = AssistantStatus::SKIPPED});
    return;
  }
  const auto& catalog = GetCatalog();
  for (auto next : GetContinuations()) {
    if (continuation_call_.empty() || next.call_id != continuation_call_) {
      continue;
    }
    continuation_call_.clear();
    if (!catalog || !ha::MatchesTarget(next, *catalog)) {
      next.kind = McpResolvedActionKind::NONE;
      next.status = AssistantStatus::ERROR;
      next.error = "DEVICE_UNREACHABLE: HA target changed during request";
    }
    if (next.status == AssistantStatus::OK && ha::NeedsContinuation(next)) {
      continuation_call_ = next.call_id;
    }
    PushAction(std::move(next));
    return;
  }
  const auto& result = GetResult();
  if (expected_generation_.empty() || result.request_id != expected_generation_) {
    PushAction(McpResolvedAction{.status = AssistantStatus::SKIPPED});
    return;
  }
  ha::Catalog missing;
  missing.status = AssistantStatus::ERROR;
  missing.error = "Missing HA catalog";
  auto action = ha::ResolveAction(result, catalog ? *catalog : missing);
  if (result.is_final) {
    expected_generation_ = action.call_id;
    continuation_call_ = action.status == AssistantStatus::OK && ha::NeedsContinuation(action)
      ? action.call_id
      : std::string();
  }
  PushAction(std::move(action));
}
void HaToolResultNode::ProcessOnce() {
  CompactToolResults results;
  std::vector<McpResolvedAction> next;
  for (const auto& completion : GetCompletions()) {
    auto out = ha::Complete(completion);
    results.insert(
      results.end(), std::make_move_iterator(out.results.begin()), std::make_move_iterator(out.results.end())
    );
    next.insert(
      next.end(), std::make_move_iterator(out.continuations.begin()), std::make_move_iterator(out.continuations.end())
    );
  }
  PushResults(std::move(results));
  PushContinuations(std::move(next));
}
} // namespace giga_edge
