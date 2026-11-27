#pragma once

#include "giga_edge/modules/mcp/log.h"
#include "giga_edge/modules/mcp/types.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {
namespace mcp {
namespace detail {

enum class PendingRequestKind {
  TOOL_CALL,
  READ_RESOURCE,
  PING,
};

struct PendingRequest {
  McpConsumerId consumer_id = 0;
  PendingRequestKind kind = PendingRequestKind::TOOL_CALL;
};

inline McpRequestToken StorePendingRequest(
  McpRequestToken& next_request_token,
  std::unordered_map<McpRequestToken, PendingRequest>& pending_requests,
  McpConsumerId consumer_id,
  PendingRequestKind kind
) {
  const auto token = next_request_token++;
  pending_requests[token] = PendingRequest{
    .consumer_id = consumer_id,
    .kind = kind,
  };
  return token;
}

inline void ErasePendingRequest(
  std::unordered_map<McpRequestToken, PendingRequest>& pending_requests, McpRequestToken token
) {
  pending_requests.erase(token);
}

inline bool IsPendingRequestOwnedBy(
  const std::unordered_map<McpRequestToken, PendingRequest>& pending_requests,
  McpRequestToken token,
  McpConsumerId consumer_id
) {
  auto it = pending_requests.find(token);
  return it != pending_requests.end() && it->second.consumer_id == consumer_id;
}

inline std::vector<McpRequestToken> ExtractPendingTokensForConsumer(
  std::unordered_map<McpRequestToken, PendingRequest>& pending_requests, McpConsumerId consumer_id
) {
  std::vector<McpRequestToken> tokens;
  for (auto it = pending_requests.begin(); it != pending_requests.end();) {
    if (it->second.consumer_id == consumer_id) {
      tokens.push_back(it->first);
      it = pending_requests.erase(it);
    } else {
      ++it;
    }
  }
  return tokens;
}

template<class TResult>
TakeBatchResult<TResult> TakeResults(std::deque<McpCompletion<TResult>>& queue, bool& overflow_flag, size_t limit) {
  TakeBatchResult<TResult> result;
  result.overflow_happened = overflow_flag;
  overflow_flag = false;

  const size_t to_take = std::min(limit, queue.size());
  result.items.reserve(to_take);
  for (size_t i = 0; i < to_take; ++i) {
    result.items.push_back(std::move(queue.front()));
    queue.pop_front();
  }
  return result;
}

template<class TResult>
void PushCompletion(
  const std::string& consumer_name,
  size_t completion_queue_capacity,
  std::deque<McpCompletion<TResult>>& queue,
  bool& overflow_flag,
  McpCompletion<TResult> completion,
  const char* queue_name
) {
  if (queue.size() >= completion_queue_capacity) {
    queue.pop_front();
    overflow_flag = true;
    WriteMcpLog(
      std::string("[WARNING] MCP coordinator: completion queue overflow for consumer '") + consumer_name + "' ("
      + queue_name + "), dropping oldest completion."
    );
  }
  queue.push_back(std::move(completion));
}

template<class TResult>
void TrimQueue(
  const std::string& consumer_name,
  size_t completion_queue_capacity,
  std::deque<McpCompletion<TResult>>& queue,
  bool& overflow_flag,
  const char* queue_name
) {
  while (queue.size() > completion_queue_capacity) {
    queue.pop_front();
    overflow_flag = true;
    WriteMcpLog(
      std::string("[WARNING] MCP coordinator: completion queue overflow for consumer '") + consumer_name + "' ("
      + queue_name + "), dropping oldest completion."
    );
  }
}

template<class ConsumersMap, class TResult, class QueueAccessor, class OverflowAccessor>
void DeliverCompletion(
  ConsumersMap& consumers,
  std::unordered_map<McpRequestToken, PendingRequest>& pending_requests,
  McpRequestToken token,
  PendingRequestKind kind,
  TResult result,
  QueueAccessor queue_accessor,
  OverflowAccessor overflow_accessor,
  const char* queue_name
) {
  auto pending_it = pending_requests.find(token);
  if (pending_it == pending_requests.end() || pending_it->second.kind != kind) {
    return;
  }

  auto consumer_it = consumers.find(pending_it->second.consumer_id);
  pending_requests.erase(pending_it);
  if (consumer_it == consumers.end()) {
    return;
  }

  auto& consumer = consumer_it->second;
  auto& queue = queue_accessor(consumer);
  auto& overflow_flag = overflow_accessor(consumer);
  PushCompletion(
    consumer.config.consumer_name,
    consumer.config.completion_queue_capacity,
    queue,
    overflow_flag,
    McpCompletion<TResult>{.token = token, .result = std::move(result)},
    queue_name
  );
}

} // namespace detail
} // namespace mcp
} // namespace giga_edge
