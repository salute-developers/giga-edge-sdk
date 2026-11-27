#pragma once

#include "giga_edge/modules/web/web_decoder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace giga_edge {
namespace web {

using WebConsumerId = uint64_t;
using WebRequestToken = uint64_t;

struct WebServiceConfig {
  uint32_t thread_pool_size = 1;
  uint32_t response_idle_timeout_ms = 90000;
  bool debug_enabled = false;
};

struct WebConsumerConfig {
  std::string consumer_name;
  std::unordered_set<std::string> visible_provider_ids;
  size_t chunk_queue_capacity = 65536;
};

template<class T>
struct TakeBatchResult {
  std::vector<T> items;
};

struct SubmitResult {
  bool accepted = false;
  WebRequestToken token = 0;
  std::string error_message;
};

} // namespace web
} // namespace giga_edge
