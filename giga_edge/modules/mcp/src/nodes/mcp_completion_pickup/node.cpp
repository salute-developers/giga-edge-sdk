#include "giga_edge/modules/mcp/nodes/mcp_completion_pickup/node.h"

#include "giga_edge/modules/assistant_core/json.h"
#include "giga_edge/modules/mcp/nodes/node_coordinator.h"

namespace giga_edge {
namespace {
// A response may outrun the recursive report of the executor: it is kept until the report arrives.
constexpr size_t kOrphanTicks = 2;

std::string OptionalJson(const Json::Value& value) {
  return value.isNull() ? std::string() : WriteJson(value);
}
} // namespace

McpCompletionPickupNode::McpCompletionPickupNode(const NodeBaseConfig& config, const McpCompletionPickupParams& params)
  : NodeBase(config)
  , params_(params)
  , coordinator_(mcp::nodes::ResolveCoordinator(config, params.environment_name, init_error_)) {
  if (!coordinator_) {
    return;
  }
  mcp::McpConsumerConfig consumer;
  consumer.consumer_name = params.consumer_name;
  consumer.visible_server_ids = {params.visible_server_ids.begin(), params.visible_server_ids.end()};
  consumer.completion_queue_capacity = params.completion_queue_capacity;
  consumer.debug_enabled = params.local_debug_enabled;
  consumer_id_ = mcp::nodes::EnsureConsumer(coordinator_, consumer);
}

McpCompletionPickupNode::~McpCompletionPickupNode() {
  if (coordinator_ && consumer_id_ && params_.owns_consumer_registration) {
    coordinator_->UnregisterConsumer(consumer_id_);
  }
}

void McpCompletionPickupNode::Reset() {
  if (coordinator_) {
    for (const auto& [token, action] : pending_) {
      (void)action;
      coordinator_->CancelRequest(consumer_id_, token);
    }
  }
  pending_.clear();
  orphans_.clear();
}

void McpCompletionPickupNode::Complete(
  mcp::McpRequestToken token, McpActionCompletion completion, McpActionCompletions& out
) {
  const auto it = pending_.find(token);
  if (it == pending_.end()) {
    orphans_[token] = Orphan{.completion = std::move(completion)};
    return;
  }
  completion.action = it->second;
  out.push_back(std::move(completion));
  pending_.erase(it);
}

void McpCompletionPickupNode::ProcessOnce() {
  const auto& query = GetUserQuery();
  McpActionCompletions out;
  const auto& report = GetSubmission();
  const auto reset = GetDialogueReset();
  if (!query.text.empty() || (reset.has_value() && reset->get())) {
    Reset();
    if (coordinator_ && report.submitted && report.request_token) {
      coordinator_->CancelRequest(consumer_id_, report.request_token);
    }
    PushCompletions(std::move(out));
    return;
  }
  if (report.status == AssistantStatus::ERROR) {
    out.push_back({.action = report.action, .is_error = true, .error = report.error});
  } else if (report.submitted && report.request_token != 0) {
    pending_.emplace(report.request_token, report.action);
  } else if (report.submitted) {
    // A subscription is not completed by a token: without an immediate completion the turn would wait for it forever.
    out.push_back({.action = report.action});
  }
  for (auto it = orphans_.begin(); it != orphans_.end();) {
    if (pending_.count(it->first) > 0) {
      Complete(it->first, std::move(it->second.completion), out);
      it = orphans_.erase(it);
    } else if (++it->second.age > kOrphanTicks) {
      it = orphans_.erase(it);
    } else {
      ++it;
    }
  }
  if (coordinator_ && consumer_id_) {
    const auto tools = coordinator_->TakeToolCallResults(consumer_id_, params_.drain_limit);
    const auto reads = coordinator_->TakeReadResourceResults(consumer_id_, params_.drain_limit);
    for (const auto& item : tools.items) {
      Complete(
        item.token,
        {.content_json = OptionalJson(item.result.content),
         .structured_content_json = OptionalJson(item.result.structured_content),
         .is_error = item.result.is_error,
         .error = item.result.error_message},
        out
      );
    }
    for (const auto& item : reads.items) {
      Complete(
        item.token,
        {.content_json = item.result.text, .is_error = item.result.is_error, .error = item.result.error_message},
        out
      );
    }
    if (tools.overflow_happened || reads.overflow_happened || pending_.size() > params_.completion_queue_capacity) {
      for (const auto& [token, action] : pending_) {
        out.push_back({.action = action, .is_error = true, .error = "MCP completion queue overflow"});
        coordinator_->CancelRequest(consumer_id_, token);
      }
      pending_.clear();
    }
  }
  PushCompletions(std::move(out));
}
} // namespace giga_edge
