#include "giga_edge/modules/web/web_service.h"

#include "giga_edge/lib/threadpool/threadpool.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace giga_edge {
namespace web {
namespace {

using Clock = std::chrono::steady_clock;

class WebServiceImpl;

// The service whose thread (a pool task or the watchdog) is running the current code.
thread_local const WebServiceImpl* t_running_service = nullptr;

class ProviderObserver : public IWebTransportObserver {
public:
  ProviderObserver(std::string provider_id, std::weak_ptr<WebServiceImpl> service)
    : provider_id_(std::move(provider_id))
    , service_(std::move(service)) {}

  void OnFrame(WebStreamId id, WebFrame frame) override;
  void OnStreamClosed(WebStreamId id, WebError error) override;

private:
  std::string provider_id_;
  std::weak_ptr<WebServiceImpl> service_;
};

class WebServiceImpl : public IWebService, public std::enable_shared_from_this<WebServiceImpl> {
public:
  explicit WebServiceImpl(WebServiceConfig config)
    : config_(config)
    , thread_pool_(
        std::make_unique<ThreadPool>(
          static_cast<int>(config.thread_pool_size > 0 ? config.thread_pool_size : 1), false, 0, "web_svc"
        )
      ) {}

  ~WebServiceImpl() override {
    WebServiceImpl::Shutdown();
  }

  void SetObserver(std::weak_ptr<IWebServiceObserver> observer) override {
    std::lock_guard<std::mutex> lock(mutex_);
    observer_ = std::move(observer);
  }

  void AddTransport(const std::string& provider_id, WebTransportPtr transport) override {
    if (!transport) {
      return;
    }
    DetachProvider(provider_id, "provider replaced");

    auto shim = std::make_shared<ProviderObserver>(provider_id, weak_from_this());
    transport->SetObserver(shim);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopping_) {
        providers_[provider_id] = Provider{std::move(transport), std::move(shim), std::make_shared<std::mutex>()};
        return;
      }
    }
    transport->SetObserver({});
  } // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): mismodeled libstdc++ shared_ptr internals

  void RemoveTransport(const std::string& provider_id) override {
    DetachProvider(provider_id, "provider removed");
  }

  void DetachProvider(const std::string& provider_id, const std::string& reason) {
    Provider removed;
    std::vector<AbandonedRequest> abandoned;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = providers_.find(provider_id);
      if (it == providers_.end()) {
        return;
      }
      removed = std::move(it->second);
      providers_.erase(it);
      opening_.erase(provider_id);
      for (auto request = requests_.begin(); request != requests_.end();) {
        if (request->second.provider_id == provider_id) {
          abandoned.push_back(
            AbandonedRequest{
              request->first,
              ForgottenRequest{request->second.decoder, request->second.session, request->second.stream_id},
            }
          );
          DropDeadlineLocked(request->second);
          request = requests_.erase(request);
        } else {
          ++request;
        }
      }
      stream_index_.erase(provider_id);
    }
    for (auto& item : abandoned) {
      FinalizeAndDeliver(item.token, item.request, WebError{WebErrorCode::TRANSPORT_ERROR, reason});
    }
    if (removed.transport) {
      removed.transport->SetObserver({});
      for (const auto& item : abandoned) {
        if (item.request.stream_id != 0) {
          removed.transport->CloseStream(item.request.stream_id);
        }
      }
    }
  }

  std::vector<std::string> GetProviderIds() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(providers_.size());
    for (const auto& entry : providers_) {
      ids.push_back(entry.first);
    }
    return ids;
  }

  bool SubmitAsync(
    WebRequestToken token,
    const std::string& provider_id,
    WebStreamDescriptor descriptor,
    std::vector<WebFrame> up_frames,
    WebResponseDecoderPtr decoder
  ) override {
    if (provider_id.empty() || !decoder) {
      return false;
    }

    WebTransportPtr transport;
    std::shared_ptr<std::mutex> open_mutex;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = providers_.find(provider_id);
      if (it == providers_.end()) {
        return false;
      }
      transport = it->second.transport;
      open_mutex = it->second.open_mutex;

      const auto now = Clock::now();
      RequestState request;
      request.provider_id = provider_id;
      request.decoder = decoder;
      request.session = std::make_shared<DecodeSession>();
      request.idle_timeout = IdleTimeout(descriptor);
      request.last_activity = now;
      auto [request_it, inserted] = requests_.emplace(token, std::move(request));
      if (!inserted) {
        return false;
      }
      if (request_it->second.idle_timeout.count() > 0) {
        ArmDeadlineLocked(token, request_it->second, now + request_it->second.idle_timeout);
      }
    }

    // The task does not own the service: otherwise the service would be destroyed on a thread of its own pool.
    // Shutdown waits for the task.
    auto work = [this,
                 token,
                 provider_id,
                 transport,
                 open_mutex,
                 descriptor = std::move(descriptor),
                 up_frames = std::move(up_frames)]() mutable {
      t_running_service = this;
      RunRequest(token, provider_id, transport, open_mutex, descriptor, std::move(up_frames));
      t_running_service = nullptr;
    };
    thread_pool_->Push(std::make_shared<Task>(std::move(work), /*priority=*/0));
    return true;
  }

  bool SendFrame(WebRequestToken token, WebFrame frame) override {
    WebTransportPtr transport;
    WebStreamId stream_id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = requests_.find(token);
      if (it == requests_.end() || it->second.send_closed) {
        return false;
      }
      if (!it->second.send_ready) {
        it->second.pending_send.push_back(std::move(frame));
        return true;
      }
      if (!ResolveTokenLocked(it, transport, stream_id)) {
        return false;
      }
    }
    return transport->SendFrame(stream_id, std::move(frame));
  }

  bool CloseSend(WebRequestToken token) override {
    WebTransportPtr transport;
    WebStreamId stream_id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = requests_.find(token);
      if (it == requests_.end() || it->second.send_closed) {
        return false;
      }
      it->second.send_closed = true;
      if (!it->second.send_ready) {
        return true;
      }
      if (!ResolveTokenLocked(it, transport, stream_id)) {
        return false;
      }
    }
    transport->CloseSend(stream_id);
    return true;
  }

  bool Cancel(WebRequestToken token) override {
    WebTransportPtr transport;
    ForgottenRequest cancelled;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = requests_.find(token);
      if (it == requests_.end()) {
        return false;
      }
      auto provider_it = providers_.find(it->second.provider_id);
      if (provider_it != providers_.end()) {
        transport = provider_it->second.transport;
      }
      cancelled = ForgetRequestLocked(it);
    }
    if (transport && cancelled.stream_id != 0) {
      transport->CloseStream(cancelled.stream_id);
    }
    FinalizeAndDeliver(token, cancelled, WebError{WebErrorCode::CANCELLED, "cancelled"});
    return true;
  }

  void Shutdown() override {
    std::map<std::string, Provider> providers;
    std::vector<std::pair<WebTransportPtr, WebStreamId>> streams;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
      for (const auto& entry : requests_) {
        auto provider_it = providers_.find(entry.second.provider_id);
        if (entry.second.stream_id != 0 && provider_it != providers_.end()) {
          streams.emplace_back(provider_it->second.transport, entry.second.stream_id);
        }
      }
      providers.swap(providers_);
      requests_.clear();
      stream_index_.clear();
      deadlines_.clear();
      opening_.clear();
      observer_.reset();
    }
    watchdog_cv_.notify_all();
    if (watchdog_.joinable()) {
      watchdog_.join();
    }
    for (auto& entry : providers) {
      if (entry.second.transport) {
        entry.second.transport->SetObserver({});
      }
    }
    for (const auto& [transport, stream_id] : streams) {
      transport->CloseStream(stream_id);
    }
    if (thread_pool_) {
      thread_pool_->Stop();
    }
  }

  void OnProviderFrame(const std::string& provider_id, WebStreamId stream_id, const WebFrame& frame) {
    ForgottenRequest found;
    WebRequestToken token = 0;
    if (!LookupRequest(provider_id, stream_id, found, token, /*mark_activity=*/true)) {
      if (config_.debug_enabled) {
        std::cerr << "[web][service] frame for unknown stream provider='" << provider_id << "' stream=" << stream_id
                  << " bytes=" << frame.bytes.size() << std::endl;
      }
      return;
    }
    DecodeAndDeliver(token, found, stream_id, frame, provider_id);
  }

  void OnProviderStreamClosed(const std::string& provider_id, WebStreamId stream_id, const WebError& error) {
    ForgottenRequest found;
    WebRequestToken token = 0;
    if (!LookupRequest(provider_id, stream_id, found, token, /*mark_activity=*/false)) {
      if (config_.debug_enabled) {
        std::cerr << "[web][service] close for unknown stream provider='" << provider_id << "' stream=" << stream_id
                  << " err='" << error.message << "'" << std::endl;
      }
      return;
    }
    ForgetRequest(token);
    FinalizeAndDeliver(token, found, error);
  }

private:
  struct DecodeSession {
    std::mutex mutex;
    bool finalized = false;
  };
  using DecodeSessionPtr = std::shared_ptr<DecodeSession>;

  struct RequestState {
    std::string provider_id;
    WebResponseDecoderPtr decoder;
    DecodeSessionPtr session;
    Clock::duration idle_timeout{};
    Clock::time_point last_activity;
    WebStreamId stream_id = 0;
    std::multimap<Clock::time_point, WebRequestToken>::iterator deadline_it;
    bool has_deadline = false;
    std::vector<WebFrame> pending_send;
    bool send_closed = false;
    bool send_ready = false;
  };
  struct Provider {
    WebTransportPtr transport;
    std::shared_ptr<ProviderObserver> observer;
    std::shared_ptr<std::mutex> open_mutex;
  };
  struct ForgottenRequest {
    WebResponseDecoderPtr decoder;
    DecodeSessionPtr session;
    WebStreamId stream_id = 0;

    explicit operator bool() const {
      return decoder != nullptr && session != nullptr;
    }
  };
  struct AbandonedRequest {
    WebRequestToken token = 0;
    ForgottenRequest request;
  };
  struct OpeningRequest {
    WebRequestToken token = 0;
    std::thread::id thread;
  };
  struct ExpiredRequest {
    WebRequestToken token = 0;
    ForgottenRequest request;
    WebTransportPtr transport;
  };

  Clock::duration IdleTimeout(const WebStreamDescriptor& descriptor) const {
    const uint32_t timeout_ms = descriptor.idle_timeout_ms.value_or(config_.response_idle_timeout_ms);
    return std::chrono::milliseconds(timeout_ms);
  }

  void RunRequest(
    WebRequestToken token,
    const std::string& provider_id,
    const WebTransportPtr& transport,
    const std::shared_ptr<std::mutex>& open_mutex,
    const WebStreamDescriptor& descriptor,
    std::vector<WebFrame> up_frames
  ) {
    WebStreamId stream_id = 0;
    bool alive = false;
    bool bound = true;

    {
      std::lock_guard<std::mutex> open_lock(*open_mutex);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requests_.find(token) == requests_.end()) {
          return;
        }
        opening_[provider_id] = OpeningRequest{token, std::this_thread::get_id()};
      }

      stream_id = transport->OpenStream(descriptor);

      std::lock_guard<std::mutex> lock(mutex_);
      opening_.erase(provider_id);
      auto it = requests_.find(token);
      alive = it != requests_.end();
      if (alive && stream_id != 0) {
        bound = BindStreamLocked(token, it->second, stream_id);
      }
    }

    if (!alive) {
      if (stream_id != 0) {
        transport->CloseStream(stream_id);
      }
      return;
    }

    if (stream_id == 0) {
      FailRequest(token, nullptr, WebError{WebErrorCode::TRANSPORT_ERROR, "OpenStream failed"});
      return;
    }

    if (!bound) {
      FailRequest(token, transport, WebError{WebErrorCode::TRANSPORT_ERROR, "transport reported another stream"});
      return;
    }

    for (auto& frame : up_frames) {
      if (transport->SendFrame(stream_id, std::move(frame))) {
        continue;
      }
      FailRequest(token, transport, WebError{WebErrorCode::TRANSPORT_ERROR, "SendFrame failed"});
      return;
    }

    while (true) {
      std::vector<WebFrame> queued;
      bool close_send = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = requests_.find(token);
        if (it == requests_.end()) {
          return;
        }
        queued.swap(it->second.pending_send);
        if (queued.empty()) {
          it->second.send_ready = true;
          close_send = it->second.send_closed || !descriptor.keep_send_open;
        }
      }
      if (queued.empty()) {
        if (close_send) {
          transport->CloseSend(stream_id);
        }
        return;
      }
      for (auto& frame : queued) {
        if (!transport->SendFrame(stream_id, std::move(frame))) {
          FailRequest(token, transport, WebError{WebErrorCode::TRANSPORT_ERROR, "SendFrame failed"});
          return;
        }
      }
    }
  }

  void FailRequest(WebRequestToken token, const WebTransportPtr& transport, const WebError& error) {
    auto failed = ForgetRequest(token);
    if (!failed) {
      return;
    }
    if (transport && failed.stream_id != 0) {
      transport->CloseStream(failed.stream_id);
    }
    FinalizeAndDeliver(token, failed, error);
  }

  bool BindStreamLocked(WebRequestToken token, RequestState& request, WebStreamId stream_id) {
    if (request.stream_id == stream_id) {
      return true;
    }
    if (request.stream_id != 0) {
      if (config_.debug_enabled) {
        std::cerr << "[web][service] stream mismatch for token=" << token << " bound=" << request.stream_id
                  << " incoming=" << stream_id << std::endl;
      }
      return false;
    }
    request.stream_id = stream_id;
    stream_index_[request.provider_id][stream_id] = token;
    return true;
  }

  void ArmDeadlineLocked(WebRequestToken token, RequestState& request, Clock::time_point deadline) {
    const bool becomes_head = deadlines_.empty() || deadline < deadlines_.begin()->first;
    request.deadline_it = deadlines_.emplace(deadline, token);
    request.has_deadline = true;
    if (becomes_head) {
      watchdog_cv_.notify_all();
    }
    if (!watchdog_.joinable() && !stopping_) {
      watchdog_ = std::thread([this]() {
        RunWatchdog();
      });
    }
  }

  void DropDeadlineLocked(RequestState& request) {
    if (request.has_deadline) {
      deadlines_.erase(request.deadline_it);
      request.has_deadline = false;
    }
  }

  ForgottenRequest ForgetRequestLocked(std::map<WebRequestToken, RequestState>::iterator it) {
    ForgottenRequest forgotten{it->second.decoder, it->second.session, it->second.stream_id};
    DropDeadlineLocked(it->second);
    if (it->second.stream_id != 0) {
      auto streams_it = stream_index_.find(it->second.provider_id);
      if (streams_it != stream_index_.end()) {
        streams_it->second.erase(it->second.stream_id);
        if (streams_it->second.empty()) {
          stream_index_.erase(streams_it);
        }
      }
    }
    requests_.erase(it);
    return forgotten;
  }

  ForgottenRequest ForgetRequest(WebRequestToken token) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(token);
    return it == requests_.end() ? ForgottenRequest{} : ForgetRequestLocked(it);
  }

  bool ResolveTokenLocked(
    std::map<WebRequestToken, RequestState>::iterator it, WebTransportPtr& transport, WebStreamId& stream_id
  ) {
    if (it->second.stream_id == 0) {
      return false;
    }
    auto provider_it = providers_.find(it->second.provider_id);
    if (provider_it == providers_.end()) {
      return false;
    }
    transport = provider_it->second.transport;
    stream_id = it->second.stream_id;
    return true;
  }

  bool LookupRequest(
    const std::string& provider_id,
    WebStreamId stream_id,
    ForgottenRequest& found,
    WebRequestToken& token,
    bool mark_activity
  ) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto streams_it = stream_index_.find(provider_id);
    if (streams_it != stream_index_.end()) {
      auto token_it = streams_it->second.find(stream_id);
      if (token_it != streams_it->second.end()) {
        auto request_it = requests_.find(token_it->second);
        if (request_it == requests_.end()) {
          return false;
        }
        if (mark_activity) {
          request_it->second.last_activity = Clock::now();
        }
        found = ForgottenRequest{request_it->second.decoder, request_it->second.session, stream_id};
        token = token_it->second;
        return true;
      }
    }

    auto opening_it = opening_.find(provider_id);
    if (opening_it == opening_.end() || opening_it->second.thread != std::this_thread::get_id()) {
      return false;
    }
    auto request_it = requests_.find(opening_it->second.token);
    if (request_it == requests_.end()) {
      return false;
    }
    if (!BindStreamLocked(opening_it->second.token, request_it->second, stream_id)) {
      return false;
    }
    if (mark_activity) {
      request_it->second.last_activity = Clock::now();
    }
    found = ForgottenRequest{request_it->second.decoder, request_it->second.session, stream_id};
    token = opening_it->second.token;
    return true;
  }

  void DecodeAndDeliver(
    WebRequestToken token,
    const ForgottenRequest& request,
    WebStreamId stream_id,
    const WebFrame& frame,
    const std::string& provider_id
  ) {
    std::vector<WebDecodedChunk> chunks;
    {
      std::lock_guard<std::mutex> lock(request.session->mutex);
      if (request.session->finalized) {
        return;
      }
      chunks = request.decoder->DecodeFrame(stream_id, frame);
    }
    if (config_.debug_enabled) {
      std::cerr << "[web][service] frame provider='" << provider_id << "' stream=" << stream_id
                << " bytes=" << frame.bytes.size() << " chunks=" << chunks.size() << std::endl;
    }
    DeliverChunks(token, chunks);
  }

  void FinalizeAndDeliver(WebRequestToken token, const ForgottenRequest& request, const WebError& error) {
    std::vector<WebDecodedChunk> chunks;
    {
      std::lock_guard<std::mutex> lock(request.session->mutex);
      if (request.session->finalized) {
        return;
      }
      request.session->finalized = true;
      chunks = request.decoder->Finalize(request.stream_id, error);
    }
    if (config_.debug_enabled) {
      std::cerr << "[web][service] finalized token=" << token << " stream=" << request.stream_id << " err='"
                << error.message << "' chunks=" << chunks.size() << std::endl;
    }
    DeliverChunks(token, chunks);
  }

  void DeliverChunks(WebRequestToken token, const std::vector<WebDecodedChunk>& chunks) {
    if (chunks.empty()) {
      return;
    }
    auto observer = [this]() {
      std::lock_guard<std::mutex> lock(mutex_);
      return observer_.lock();
    }();
    if (!observer) {
      return;
    }
    for (const auto& chunk : chunks) {
      observer->OnChunk(token, chunk);
    }
  }

  std::vector<ExpiredRequest> TakeExpiredLocked(Clock::time_point now) {
    std::vector<ExpiredRequest> expired;
    while (!deadlines_.empty() && deadlines_.begin()->first <= now) {
      const WebRequestToken token = deadlines_.begin()->second;
      auto it = requests_.find(token);
      if (it == requests_.end()) {
        deadlines_.erase(deadlines_.begin());
        continue;
      }

      const auto due = it->second.last_activity + it->second.idle_timeout;
      if (due > now) {
        deadlines_.erase(deadlines_.begin());
        it->second.has_deadline = false;
        ArmDeadlineLocked(token, it->second, due);
        continue;
      }

      ExpiredRequest item;
      item.token = token;
      auto provider_it = providers_.find(it->second.provider_id);
      if (provider_it != providers_.end()) {
        item.transport = provider_it->second.transport;
      }
      item.request = ForgetRequestLocked(it);
      expired.push_back(std::move(item));
    }
    return expired;
  }

  void RunWatchdog() {
    t_running_service = this;
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_) {
      if (deadlines_.empty()) {
        watchdog_cv_.wait(lock, [this]() {
          return stopping_ || !deadlines_.empty();
        });
        continue;
      }
      const auto next = deadlines_.begin()->first;
      if (watchdog_cv_.wait_until(lock, next, [this, next]() {
            return stopping_ || deadlines_.empty() || deadlines_.begin()->first < next;
          })) {
        continue;
      }

      auto expired = TakeExpiredLocked(Clock::now());
      if (expired.empty()) {
        continue;
      }
      lock.unlock();
      for (auto& item : expired) {
        if (item.request.stream_id != 0 && item.transport) {
          item.transport->CloseStream(item.request.stream_id);
        }
        FinalizeAndDeliver(item.token, item.request, WebError{WebErrorCode::TIMEOUT, "request timed out"});
      }
      lock.lock();
    }
  }

  WebServiceConfig config_;
  std::unique_ptr<IThreadPool> thread_pool_;

  mutable std::mutex mutex_;
  std::weak_ptr<IWebServiceObserver> observer_;
  std::map<std::string, Provider> providers_;
  std::map<WebRequestToken, RequestState> requests_;
  std::map<std::string, std::map<WebStreamId, WebRequestToken>> stream_index_;
  std::map<std::string, OpeningRequest> opening_;
  std::multimap<Clock::time_point, WebRequestToken> deadlines_;

  std::condition_variable watchdog_cv_;
  std::thread watchdog_;
  bool stopping_ = false;
};

void ProviderObserver::OnFrame(WebStreamId id, WebFrame frame) {
  if (auto service = service_.lock()) {
    service->OnProviderFrame(provider_id_, id, frame);
  }
}

void ProviderObserver::OnStreamClosed(WebStreamId id, WebError error) {
  if (auto service = service_.lock()) {
    service->OnProviderStreamClosed(provider_id_, id, error);
  }
}

} // namespace

std::shared_ptr<IWebService> CreateWebService(WebServiceConfig config) {
  // The destruction contract is described in web_service.h.
  return std::shared_ptr<WebServiceImpl>(new WebServiceImpl(config), [](WebServiceImpl* service) {
    if (t_running_service == service) {
      std::thread([service]() {
        delete service;
      }).detach();
    } else {
      delete service;
    }
  });
}

} // namespace web
} // namespace giga_edge
