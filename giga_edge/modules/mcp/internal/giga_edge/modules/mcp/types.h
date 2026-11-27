#pragma once

#include "giga_edge/modules/mcp/connection.h"

#include <json/json.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace giga_edge {
namespace mcp {

using McpConsumerId = uint64_t;
using McpRequestToken = uint64_t;
using McpVersion = uint64_t;
using McpSubscriptionOperationId = uint64_t;

struct McpResourceHydrationConfig {
  bool read_resources_on_list = false;
  bool subscribe_to_subscribable_resources = false;
  std::vector<std::string> preferred_read_resource_match_phrases;
  uint32_t retry_initial_ms = 1000;
  uint32_t retry_max_ms = 30000;
  std::vector<int32_t> no_retry_error_codes = {
    static_cast<int32_t>(ErrorCode::INVALID_REQUEST),
    static_cast<int32_t>(ErrorCode::METHOD_NOT_FOUND),
    static_cast<int32_t>(ErrorCode::INVALID_PARAMS),
  };
};

struct McpServiceConfig {
  std::string protocol_version = "2024-11-05";
  std::string client_name = "giga-edge";
  std::string client_version = "1.0";
  uint32_t thread_pool_size = 4;
  uint32_t default_request_timeout_ms = kDefaultRequestTimeoutMs;
  uint32_t default_tool_call_timeout_ms = kDefaultRequestTimeoutMs;
  uint32_t default_read_resource_timeout_ms = kDefaultRequestTimeoutMs;
  uint32_t default_ping_timeout_ms = kDefaultRequestTimeoutMs;
  bool debug_enabled = false;
  McpResourceHydrationConfig resource_hydration;
};

struct McpToolDescriptor {
  std::string server_id;
  std::string tool_name;
  std::string description;
  Json::Value input_schema;
  std::string title;
};

struct McpToolCallRequest {
  std::string call_id;
  std::string server_id;
  std::string tool_name;
  Json::Value arguments;
  uint32_t timeout_ms = 0;
  // Optional params._meta object; null means the field is absent.
  Json::Value metadata;
};

struct McpToolCallResultEvent {
  std::string call_id;
  std::string server_id;
  std::string tool_name;
  bool is_error = false;
  std::string error_message;
  Json::Value content;
  Json::Value structured_content;
  // Client/protocol error code; a tool response with is_error may still have NONE.
  ErrorCode error_code = ErrorCode::NONE;
  // false means the request was not sent to the transport and is safe to retry: cancellation and sending are decided
  // under one lock.
  bool request_sent = false;
};

struct McpServerStatusEvent {
  std::string server_id;

  enum class Status { CONNECTED, DISCONNECTED, ERROR } status;

  std::string message;
};

struct McpResourceDescriptor {
  std::string server_id;
  std::string uri;
  std::string name;
  std::string mime_type;
  std::string description;
  bool subscribable = false;
};

struct McpReadResourceRequest {
  std::string call_id;
  std::string server_id;
  std::string uri;
  uint32_t timeout_ms = 0;
};

struct McpReadResourceResultEvent {
  std::string call_id;
  std::string server_id;
  std::string uri;
  bool is_error = false;
  std::string error_message;
  std::string mime_type;
  std::string text;
};

struct McpResourceTemplateDescriptor {
  std::string server_id;
  std::string uri_template;
  std::string name;
  std::string mime_type;
  std::string description;
};

struct McpResourceUpdatedEvent {
  std::string server_id;
  std::string uri;
  Json::Value data;
};

struct McpPingRequest {
  std::string call_id;
  std::string server_id;
  uint32_t timeout_ms = 0;
};

struct McpPingResultEvent {
  std::string call_id;
  std::string server_id;
  bool is_error = false;
  std::string error_message;
};

struct McpConsumerConfig {
  std::string consumer_name;
  std::unordered_set<std::string> visible_server_ids;
  size_t completion_queue_capacity = 4096;
  bool include_shared_latest_resource_states = false;
  bool debug_enabled = false;
};

template<class T>
struct VersionedSnapshot {
  McpVersion version = 0;
  std::shared_ptr<const T> data;
};

template<class TResult>
struct McpCompletion {
  McpRequestToken token = 0;
  TResult result;
};

template<class TResult>
struct TakeBatchResult {
  std::vector<McpCompletion<TResult>> items;
  bool overflow_happened = false;
};

struct McpLatestResourceState {
  std::string server_id;
  std::string uri;
  Json::Value data;
};

enum class McpResourceSubscriptionStatus {
  NOT_SUBSCRIBED,
  SUBSCRIBE_PENDING,
  SUBSCRIBED,
  SUBSCRIBE_FAILED,
  UNSUBSCRIBE_PENDING,
};

struct McpResourceSubscriptionState {
  std::string server_id;
  std::string uri;
  McpResourceSubscriptionStatus status = McpResourceSubscriptionStatus::NOT_SUBSCRIBED;
  std::string error_message;
};

struct McpResourceSubscriptionEvent {
  enum class Operation {
    SUBSCRIBE,
    UNSUBSCRIBE,
  };

  McpSubscriptionOperationId operation_id = 0;
  std::string server_id;
  std::string uri;
  Operation operation = Operation::SUBSCRIBE;
  bool is_error = false;
  std::string error_message;
};

struct SubmitResult {
  bool accepted = false;
  McpRequestToken token = 0;
  std::string error_message;
};

} // namespace mcp
} // namespace giga_edge
