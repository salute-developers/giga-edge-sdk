#include "giga_edge/modules/web/web_coordinator.h"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge {
namespace web {
namespace {

WebDecodedChunk MakeOverflowChunk(const std::string& consumer_name, size_t capacity) {
  return MakeErrorChunk(
    "web chunk queue overflow for consumer '" + consumer_name + "' (capacity " + std::to_string(capacity)
    + "), request dropped"
  );
}

class WebCoordinatorImpl
  : public IWebCoordinator,
    public IWebServiceObserver,
    public std::enable_shared_from_this<WebCoordinatorImpl> {
public:
  explicit WebCoordinatorImpl(std::shared_ptr<IWebService> service)
    : service_(std::move(service)) {}

  WebConsumerId RegisterConsumer(WebConsumerConfig config) override {
    std::lock_guard<std::mutex> lock(mutex_);
    return RegisterConsumerLocked(std::move(config));
  }

  WebConsumerId EnsureConsumer(WebConsumerConfig config) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : consumers_) {
      if (entry.second.config.consumer_name == config.consumer_name) {
        ++entry.second.owners;
        return entry.first;
      }
    }
    return RegisterConsumerLocked(std::move(config));
  }

  void UnregisterConsumer(WebConsumerId consumer_id) override {
    std::vector<WebRequestToken> abandoned;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = consumers_.find(consumer_id);
      if (it == consumers_.end()) {
        return;
      }
      if (--it->second.owners > 0) {
        return;
      }
      for (auto request = requests_.begin(); request != requests_.end();) {
        if (request->second.consumer_id == consumer_id) {
          abandoned.push_back(request->first);
          request = requests_.erase(request);
        } else {
          ++request;
        }
      }
      consumers_.erase(it);
    }
    CancelTokens(abandoned);
  }

  SubmitResult Submit(
    WebConsumerId consumer_id,
    const std::string& provider_id,
    WebStreamDescriptor descriptor,
    std::vector<WebFrame> up_frames,
    WebResponseDecoderPtr decoder
  ) override {
    if (provider_id.empty()) {
      return SubmitResult{false, 0, "provider_id is required"};
    }

    WebRequestToken token = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = consumers_.find(consumer_id);
      if (it == consumers_.end()) {
        return SubmitResult{false, 0, "unknown consumer"};
      }
      if (!IsProviderVisible(it->second.config, provider_id)) {
        return SubmitResult{
          false,
          0,
          "provider '" + provider_id + "' is not visible to consumer '" + it->second.config.consumer_name + "'"
        };
      }
      token = next_token_++;
      requests_.emplace(token, Request{consumer_id});
    }

    if (!service_) {
      DropRequest(token);
      return SubmitResult{false, 0, "web service is not available"};
    }
    if (!service_->SubmitAsync(token, provider_id, std::move(descriptor), std::move(up_frames), std::move(decoder))) {
      DropRequest(token);
      return SubmitResult{
        false, 0, "web service rejected the request for provider '" + provider_id + "' (no such provider or no decoder)"
      };
    }
    return SubmitResult{true, token, ""};
  }

  bool SendFrame(WebConsumerId consumer_id, WebRequestToken token, WebFrame frame) override {
    if (!OwnsToken(consumer_id, token)) {
      return false;
    }
    return service_ && service_->SendFrame(token, std::move(frame));
  }

  bool CloseSend(WebConsumerId consumer_id, WebRequestToken token) override {
    if (!OwnsToken(consumer_id, token)) {
      return false;
    }
    return service_ && service_->CloseSend(token);
  }

  bool CancelRequest(WebConsumerId consumer_id, WebRequestToken token) override {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = requests_.find(token);
      if (it == requests_.end() || it->second.consumer_id != consumer_id) {
        return false;
      }
      requests_.erase(it);
    }
    if (service_) {
      service_->Cancel(token);
    }
    return true;
  }

  TakeBatchResult<WebDecodedChunk> TakeChunkResults(
    WebConsumerId consumer_id, WebRequestToken token, size_t limit
  ) override {
    std::lock_guard<std::mutex> lock(mutex_);
    TakeBatchResult<WebDecodedChunk> result;
    auto it = requests_.find(token);
    if (it == requests_.end() || it->second.consumer_id != consumer_id) {
      return result;
    }
    Request& request = it->second;
    const size_t take = std::min(limit, request.queue.size());
    result.items.reserve(take);
    for (size_t i = 0; i < take; ++i) {
      result.items.push_back(std::move(request.queue.front()));
      request.queue.pop_front();
    }
    if (request.finished && request.queue.empty()) {
      requests_.erase(it);
    }
    return result;
  }

  bool HasRequest(WebConsumerId consumer_id, WebRequestToken token) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(token);
    return it != requests_.end() && it->second.consumer_id == consumer_id;
  }

  void OnChunk(WebRequestToken token, WebDecodedChunk chunk) override {
    bool overflowed = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = requests_.find(token);
      if (it == requests_.end()) {
        return;
      }
      Request& request = it->second;
      if (request.finished) {
        return;
      }

      auto consumer_it = consumers_.find(request.consumer_id);
      if (consumer_it == consumers_.end()) {
        requests_.erase(it);
        return;
      }

      const size_t capacity = consumer_it->second.config.chunk_queue_capacity;
      if (capacity > 0 && request.queue.size() >= capacity) {
        request.queue.clear();
        request.queue.push_back(MakeOverflowChunk(consumer_it->second.config.consumer_name, capacity));
        request.finished = true;
        overflowed = true;
      } else {
        request.finished = chunk.meta.is_final;
        request.queue.push_back(std::move(chunk));
      }
    }
    if (overflowed && service_) {
      service_->Cancel(token);
    }
  }

private:
  struct Consumer {
    WebConsumerConfig config;
    size_t owners = 1;
  };

  struct Request {
    WebConsumerId consumer_id = 0;
    std::deque<WebDecodedChunk> queue;
    bool finished = false;
  };

  static bool IsProviderVisible(const WebConsumerConfig& config, const std::string& provider_id) {
    return config.visible_provider_ids.empty() || config.visible_provider_ids.count(provider_id) > 0;
  }

  WebConsumerId RegisterConsumerLocked(WebConsumerConfig config) {
    const WebConsumerId id = next_consumer_id_++;
    consumers_.emplace(id, Consumer{std::move(config)});
    return id;
  }

  void DropRequest(WebRequestToken token) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_.erase(token);
  }

  void CancelTokens(const std::vector<WebRequestToken>& tokens) {
    if (!service_) {
      return;
    }
    for (const auto token : tokens) {
      service_->Cancel(token);
    }
  }

  bool OwnsToken(WebConsumerId consumer_id, WebRequestToken token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(token);
    return it != requests_.end() && it->second.consumer_id == consumer_id;
  }

  std::shared_ptr<IWebService> service_;

  mutable std::mutex mutex_;
  WebConsumerId next_consumer_id_ = 1;
  WebRequestToken next_token_ = 1;
  std::map<WebConsumerId, Consumer> consumers_;
  std::unordered_map<WebRequestToken, Request> requests_;
};

} // namespace

std::shared_ptr<IWebCoordinator> CreateWebCoordinator(std::shared_ptr<IWebService> service) {
  auto coordinator = std::make_shared<WebCoordinatorImpl>(service);
  if (service) {
    service->SetObserver(std::static_pointer_cast<IWebServiceObserver>(coordinator));
  }
  return coordinator;
}

} // namespace web
} // namespace giga_edge
