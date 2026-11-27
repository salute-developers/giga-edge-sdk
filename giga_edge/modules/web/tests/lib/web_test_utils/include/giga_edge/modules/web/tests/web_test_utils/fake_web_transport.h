#pragma once

#include "giga_edge/modules/web/tests/web_test_utils/wait_for.h"
#include "giga_edge/modules/web/web_transport.h"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace giga_edge {
namespace web {
namespace test_utils {

class FakeWebTransport : public IWebTransportWithShutdown {
public:
  void SetObserver(std::weak_ptr<IWebTransportObserver> observer) override {
    std::lock_guard<std::mutex> lock(mutex_);
    observer_ = std::move(observer);
  }

  WebStreamId OpenStream(const WebStreamDescriptor& descriptor) override {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      ++open_entered_count_;
      open_entered_cv_.notify_all();
      open_gate_cv_.wait(lock, [this]() {
        return !open_blocked_;
      });
    }

    WebStreamId id = 0;
    std::string eager;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_) {
        return 0;
      }
      id = next_id_++;
      opened_.push_back(id);
      open_.insert(id);
      descriptors_[id] = descriptor;
      eager = eager_reply_;
    }
    if (!eager.empty()) {
      Respond(id, eager);
    }
    return id;
  }

  bool SendFrame(WebStreamId id, WebFrame frame) override {
    std::lock_guard<std::mutex> lock(mutex_);
    // The transport contract: false after CloseSend and for an unknown id.
    const bool send_closed = std::find(close_send_.begin(), close_send_.end(), id) != close_send_.end();
    if (!send_frame_result_ || open_.count(id) == 0 || send_closed) {
      return false;
    }
    sent_[id].emplace_back(frame.bytes.begin(), frame.bytes.end());
    return true;
  }

  void CloseSend(WebStreamId id) override {
    std::string echo;
    bool echo_mode = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      close_send_.push_back(id);
      echo_mode = echo_mode_;
      if (echo_mode) {
        auto it = sent_.find(id);
        if (it != sent_.end()) {
          for (const auto& frame : it->second) {
            echo += frame;
          }
        }
      }
    }
    if (echo_mode) {
      Respond(id, echo);
    }
  }

  void CloseStream(WebStreamId id) override {
    bool silent = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      close_stream_.push_back(id);
      silent = silent_close_;
    }
    if (silent) {
      return;
    }
    EmitClose(id, WebError{WebErrorCode::CANCELLED, "cancelled"});
  }

  // The IWebTransportWithShutdown contract: open streams are cancelled with CANCELLED, the call is idempotent, and
  // there are no callbacks after it returns (delivery is synchronous: there are no "threads").
  void Shutdown() noexcept override {
    std::vector<WebStreamId> open;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open.assign(open_.begin(), open_.end());
      open_.clear();
    }
    for (const WebStreamId id : open) {
      EmitClose(id, WebError{WebErrorCode::CANCELLED, "cancelled"});
    }
  }

  void Emit(WebStreamId id, const std::string& text, bool end_of_stream = false) {
    auto observer = Observer();
    if (!observer) {
      return;
    }
    WebFrame frame;
    frame.bytes.assign(text.begin(), text.end());
    frame.end_of_stream = end_of_stream;
    observer->OnFrame(id, std::move(frame));
  }

  // The head of the synthesized response: once per stream, before the frames, as in the curl transport.
  void EmitHead(WebStreamId id) {
    WebResponseHead head;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      head = response_head_;
    }
    auto observer = Observer();
    if (observer) {
      observer->OnResponseHead(id, std::move(head));
    }
  }

  // The full response of the fake in one piece: head, eos frame, close. Both synthesis modes (eager/echo) call only
  // this method, so the shape of the response is defined in one place.
  void Respond(WebStreamId id, const std::string& text) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_.erase(id);
    }
    EmitHead(id);
    Emit(id, text, /*end_of_stream=*/true);
    EmitClose(id, WebError{});
  }

  void EmitClose(WebStreamId id, WebError error) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_.erase(id);
    }
    auto observer = Observer();
    if (observer) {
      observer->OnStreamClosed(id, error);
    }
  }

  void SetConnected(bool connected) {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = connected;
  }

  void SetEchoMode(bool echo_mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    echo_mode_ = echo_mode;
  }

  void SetSendFrameResult(bool result) {
    std::lock_guard<std::mutex> lock(mutex_);
    send_frame_result_ = result;
  }

  void SetSilentClose(bool silent) {
    std::lock_guard<std::mutex> lock(mutex_);
    silent_close_ = silent;
  }

  void SetEagerReply(std::string text) {
    std::lock_guard<std::mutex> lock(mutex_);
    eager_reply_ = std::move(text);
  }

  void SetResponseHead(WebResponseHead head) {
    std::lock_guard<std::mutex> lock(mutex_);
    response_head_ = std::move(head);
  }

  void BlockOpenStream() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_blocked_ = true;
  }

  void ReleaseOpenStream() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_blocked_ = false;
    }
    open_gate_cv_.notify_all();
  }

  bool WaitUntilInsideOpenStream(size_t expected = 1) {
    std::unique_lock<std::mutex> lock(mutex_);
    return open_entered_cv_.wait_for(lock, kWaitHangGuard, [this, expected]() {
      return open_entered_count_ >= expected;
    });
  }

  size_t OpenEnteredCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_entered_count_;
  }

  // The service sends the request frames only after binding the stream to the request: from that moment the frames
  // and the close of the stream reach the request. Returns the stream number index in the order of opening if
  // frames have already been sent to it, otherwise 0.
  WebStreamId SentStream(size_t index = 0) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index < opened_.size() && sent_.count(opened_[index]) != 0 ? opened_[index] : 0;
  }

  WebStreamId WaitSentStream(size_t index = 0) const {
    WaitFor([this, index]() {
      return SentStream(index) != 0;
    });
    return SentStream(index);
  }

  size_t OpenStreamCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return opened_.size();
  }

  std::vector<WebStreamId> OpenedStreams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return opened_;
  }

  WebStreamDescriptor DescriptorOf(WebStreamId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptors_.find(id);
    return it == descriptors_.end() ? WebStreamDescriptor{} : it->second;
  }

  std::vector<std::string> SentFrames(WebStreamId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sent_.find(id);
    return it == sent_.end() ? std::vector<std::string>{} : it->second;
  }

  std::vector<WebStreamId> CloseSendCalls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return close_send_;
  }

  std::vector<WebStreamId> CloseStreamCalls() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return close_stream_;
  }

  bool HasObserver() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !observer_.expired();
  }

private:
  std::shared_ptr<IWebTransportObserver> Observer() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return observer_.lock();
  }

  mutable std::mutex mutex_;
  std::weak_ptr<IWebTransportObserver> observer_;
  bool connected_ = true;
  bool echo_mode_ = false;
  std::string eager_reply_;
  // The synthesized response of the fake is always successful: the head defaults to 200.
  WebResponseHead response_head_{/*status=*/200};
  bool send_frame_result_ = true;
  bool silent_close_ = false;
  bool open_blocked_ = false;
  size_t open_entered_count_ = 0;
  std::condition_variable open_gate_cv_;
  std::condition_variable open_entered_cv_;
  WebStreamId next_id_ = 1;
  std::vector<WebStreamId> opened_;
  std::map<WebStreamId, WebStreamDescriptor> descriptors_;
  std::map<WebStreamId, std::vector<std::string>> sent_;
  std::vector<WebStreamId> close_send_;
  std::vector<WebStreamId> close_stream_;
  std::set<WebStreamId> open_;
};

inline const std::string* HeaderValueOf(
  const std::vector<std::pair<std::string, std::string>>& headers, const std::string& name
) {
  for (const auto& header : headers) {
    if (header.first == name) {
      return &header.second;
    }
  }
  return nullptr;
}

// Response headers arrive in the case used by the server: all values of a name are collected case-insensitively
// (HeaderValueOf returns the first match, for requests with the lowercase names of the server).
inline std::vector<std::string> HeaderValuesOf(
  const std::vector<std::pair<std::string, std::string>>& headers, const std::string& name
) {
  std::vector<std::string> values;
  for (const auto& header : headers) {
    if (
      header.first.size() == name.size()
      && std::equal(name.begin(), name.end(), header.first.begin(), [](unsigned char a, unsigned char b) {
           return std::tolower(a) == std::tolower(b);
         })
    ) {
      values.push_back(header.second);
    }
  }
  return values;
}

} // namespace test_utils
} // namespace web
} // namespace giga_edge
