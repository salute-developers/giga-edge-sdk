#include "giga_edge/modules/web_transport_curl/curl_web_transport.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge {
namespace web_transport_curl {
namespace {

using web::WebError;
using web::WebErrorCode;
using web::WebFrame;
using web::WebResponseHead;
using web::WebStreamDescriptor;
using web::WebStreamId;

void EnsureCurlGlobalInit() {
  static const bool initialized = []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)initialized;
}

constexpr size_t kErrorBodyCap = 64 * 1024;

const char* const kSensitiveVerboseHeaders[] = {"authorization:", "cookie:", "set-cookie:", "proxy-authorization:"};

bool IEquals(std::string_view left, std::string_view right) {
  return left.size() == right.size()
    && std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b) {
           return std::tolower(a) == std::tolower(b);
         });
}

// The default curl verbose printer emits headers as is, including Authorization (Bearer tokens, the SigV4
// Credential with the access key) and Cookie. Values of sensitive header names are redacted; everything else is
// printed in the format of the default curl printer.
bool HasSensitiveHeaderName(std::string_view line) {
  for (const char* const name : kSensitiveVerboseHeaders) {
    const std::string_view pattern(name);
    if (line.size() >= pattern.size() && IEquals(line.substr(0, pattern.size()), pattern)) {
      return true;
    }
  }
  return false;
}

// The header name is compared case-insensitively: curl internally (Curl_checkheaders, SigV4 signing, the Expect
// logic) sees the headers as a list and takes the first match, yet sends all custom headers on the wire without
// deduplication, so a duplicated name breaks the request.
bool HasHeader(const std::vector<std::pair<std::string, std::string>>& headers, std::string_view name) {
  for (const auto& header : headers) {
    if (IEquals(header.first, name)) {
      return true;
    }
  }
  return false;
}

// HEAD has no request body per the RFC. The "method without a body" decision lives under one name for both the API
// gate (SendFrame rejects frames) and the curl setup (UPLOAD is not set).
bool HasRequestBody(const std::string& method) {
  return method != "HEAD";
}

int RedactingDebugCb(CURL*, curl_infotype type, char* data, size_t size, void*) {
  if (type == CURLINFO_TEXT) {
    std::cerr << "* ";
    std::cerr.write(data, static_cast<std::streamsize>(size));
    return 0;
  }
  if (type != CURLINFO_HEADER_OUT && type != CURLINFO_HEADER_IN) {
    return 0; // the default printer does not print bodies and TLS dumps either
  }
  const char direction = type == CURLINFO_HEADER_OUT ? '>' : '<';
  size_t begin = 0;
  while (begin < size) {
    const void* newline = std::memchr(data + begin, '\n', size - begin);
    const size_t line_len = newline != nullptr
      ? static_cast<size_t>(static_cast<const char*>(newline) - (data + begin)) + 1
      : size - begin;
    const std::string_view line(data + begin, line_len);
    if (HasSensitiveHeaderName(line)) {
      const size_t name_len = line.find(':');
      std::cerr << direction << ' ';
      std::cerr.write(
        line.data(), static_cast<std::streamsize>(name_len == std::string_view::npos ? line.size() : name_len)
      );
      std::cerr << ": [redacted]\n";
    } else {
      std::cerr << direction << ' ';
      std::cerr.write(line.data(), static_cast<std::streamsize>(line.size()));
    }
    begin += line_len;
  }
  return 0;
}

} // namespace

struct CurlWebTransport::Impl : std::enable_shared_from_this<Impl> {
  // The send queue and the flags are guarded by mutex; API threads also read easy under it. header_list,
  // http_status, error_prefix and the head delivery latch (response_head_sent) are used only by the worker thread
  // (RunLoop below), and so is by_easy. multi belongs to the worker thread; of the API threads only WakeWorker
  // touches it: the read and the wakeup happen under mutex, and the cleanup (CleanupMulti) under the same mutex.
  struct Stream {
    Impl* impl = nullptr;
    WebStreamId id = 0;
    std::string method = "POST";
    std::string path;
    std::vector<std::pair<std::string, std::string>> headers;
    bool keep_send_open = false;

    std::deque<std::shared_ptr<const WebFrame>> frames;
    size_t frame_offset = 0;
    uint64_t total_bytes = 0;
    bool send_closed = false;
    bool commit_requested = false;
    bool unpause_requested = false;
    bool cancel_requested = false;

    CURL* easy = nullptr;
    curl_slist* header_list = nullptr;
    char error[CURL_ERROR_SIZE] = {};
    long http_status = 0;
    std::string error_prefix;
    bool response_head_sent = false;
  };

  Impl(std::string url, const CurlWebTransportOptions& opts)
    : base_url(std::move(url))
    , options(opts) {
    EnsureCurlGlobalInit();
    multi = curl_multi_init();
    if (options.max_connections > 0) {
      curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, static_cast<long>(options.max_connections));
    }
  }

  void Shutdown() {
    std::thread::id worker_id;
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
      worker_id = this->worker_id;
    }
    WakeWorker(); // cv does not wake curl_multi_poll: without the wakeup the wait could last up to a second
    // A call from an observer callback runs on the worker thread itself: it cannot wait for itself, the signal is
    // already set and Run will finish on its own. The remaining streams are closed with CANCELLED asynchronously.
    // A default-constructed id equals the id of no thread: it means the worker has not started.
    if (worker_id == std::thread::id{} || worker_id == std::this_thread::get_id()) {
      return;
    }
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&]() {
      return worker_done;
    });
  }

  ~Impl() {
    Shutdown();
    CleanupMulti();
  }

  void SetObserver(std::weak_ptr<web::IWebTransportObserver> observer) {
    std::lock_guard<std::mutex> lock(mutex);
    observer_weak = std::move(observer);
  }

  WebStreamId OpenStream(const WebStreamDescriptor& descriptor) {
    auto stream = std::make_unique<Stream>();
    stream->impl = this;
    for (const auto& header : descriptor.headers) {
      const std::string& name = header.first;
      if (name.empty() || name.front() == ':') {
        if (name == ":method" && !header.second.empty()) {
          stream->method = header.second;
        } else if (name == ":path" && !header.second.empty() && header.second.front() == '/') {
          stream->path = header.second;
        } else {
          return 0;
        }
      } else {
        stream->headers.push_back(header);
      }
    }
    stream->keep_send_open = descriptor.keep_send_open;

    WebStreamId id = 0;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (stopping) {
        return 0;
      }
      id = next_id++;
      stream->id = id;
      streams.emplace(id, std::move(stream));
      // The stream is accepted and the worker is started under one lock: otherwise Shutdown could miss an accepted
      // stream and leave it without a terminal callback.
      if (worker_id == std::thread::id{}) {
        // Self-hold: the started thread keeps Impl alive for the whole Run, so destroying the transport from a
        // callback (the worker thread) does not tear the state down underneath it; the last reference is released
        // at the end of Run, where the terminal cleanup (WorkerExited) also runs.
        // Detach: the worker cannot join itself; completion is awaited through worker_done.
        std::thread runner([self = shared_from_this()]() {
          self->Run();
        });
        worker_id = runner.get_id();
        runner.detach();
      }
    }
    return id;
  }

  bool SendFrame(WebStreamId id, WebFrame frame) {
    return SendFrameShared(id, std::make_shared<WebFrame>(std::move(frame)));
  }

  bool SendFrameShared(WebStreamId id, std::shared_ptr<const WebFrame> frame) {
    if (frame == nullptr) {
      return false;
    }
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = streams.find(id);
      if (it == streams.end() || it->second->send_closed) {
        return false;
      }
      Stream* s = it->second.get();
      if (!HasRequestBody(s->method)) {
        // Frames on a method without a body would mean UPLOAD, and in curl UPLOAD resets CURLOPT_NOBODY (setopt.c:
        // opt_no_body = FALSE, "this is implied"), so HEAD would hang waiting for a response body.
        return false;
      }
      s->total_bytes += frame->bytes.size();
      s->frames.push_back(std::move(frame));
      notify = NoteSendProgressLocked(s, s->keep_send_open);
    }
    if (notify) {
      WakeWorker();
    }
    return true;
  }

  void CloseSend(WebStreamId id) {
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = streams.find(id);
      if (it == streams.end() || it->second->send_closed) {
        return;
      }
      Stream* s = it->second.get();
      s->send_closed = true;
      notify = NoteSendProgressLocked(s, true);
    }
    if (notify) {
      WakeWorker();
    }
  }

  void CloseStream(WebStreamId id) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = streams.find(id);
      if (it == streams.end() || it->second->cancel_requested) {
        return;
      }
      it->second->cancel_requested = true;
    }
    WakeWorker();
  }

  bool NoteSendProgressLocked(Stream* s, bool commit_now) {
    if (commit_now && s->easy == nullptr) {
      s->commit_requested = true;
      return true;
    }
    if (s->easy != nullptr) {
      s->unpause_requested = true;
      return true;
    }
    return false;
  }

  void WakeWorker() {
    cv.notify_one();
    std::lock_guard<std::mutex> lock(mutex);
    if (multi != nullptr) {
      curl_multi_wakeup(multi);
    }
  }

  // The terminal cleanup and the publication of worker_done must happen on every exit from the worker, normal or
  // exceptional; RAII is the only way to guarantee it.
  struct WorkerExitSentry {
    Impl& impl;

    ~WorkerExitSentry() {
      impl.WorkerExited();
    }
  };

  void Run() {
    const WorkerExitSentry sentry{*this};
    try {
      RunLoop();
    } catch (const std::exception& e) {
      std::cerr << "CurlWebTransport: worker loop failed: " << e.what() << std::endl;
    } catch (...) {
      std::cerr << "CurlWebTransport: worker loop failed: unknown exception" << std::endl;
    }
  }

  void WorkerExited() noexcept {
    // The death of the worker is final: there is no restart. stopping is set BEFORE the closing callbacks are
    // delivered: otherwise OpenStream (including a reentrant call from the callback itself) would accept a stream
    // that nobody can serve, and it would wait for its terminal callback forever with no diagnostics. The snapshot
    // taken in the same section is stable: once stopping is set, OpenStream no longer adds streams.
    std::vector<Stream*> remaining;
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
      remaining.reserve(streams.size());
      for (const auto& entry : streams) {
        remaining.push_back(entry.second.get());
      }
    }
    for (Stream* stream : remaining) {
      DeliverClosed(stream, WebError{WebErrorCode::TRANSPORT_ERROR, "worker exited"});
      DestroyStream(stream);
    }
    CleanupMulti();
    {
      std::lock_guard<std::mutex> lock(mutex);
      worker_done = true;
    }
    cv.notify_all();
  }

  void CleanupMulti() {
    std::lock_guard<std::mutex> lock(mutex);
    if (multi != nullptr) {
      curl_multi_cleanup(multi);
      multi = nullptr;
    }
  }

  void RunLoop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
      std::vector<Stream*> to_cancel;
      std::vector<Stream*> to_commit;
      std::vector<Stream*> to_unpause;
      for (auto& entry : streams) {
        Stream* s = entry.second.get();
        if (s->cancel_requested || stopping) {
          // A cancelled stream must not get into a second vector: the cancel branch frees its memory.
          to_cancel.push_back(s);
          continue;
        }
        if (s->commit_requested && s->easy == nullptr) {
          to_commit.push_back(s);
        }
        if (s->unpause_requested && s->easy != nullptr) {
          to_unpause.push_back(s);
        }
      }
      if (to_cancel.empty() && to_commit.empty() && to_unpause.empty() && by_easy.empty()) {
        if (stopping) {
          break;
        }
        cv.wait(lock, [&]() {
          return stopping || !by_easy.empty() || HasPendingWorkLocked();
        });
        continue;
      }
      lock.unlock();

      for (auto* s : to_cancel) {
        CancelNow(s);
      }
      for (auto* s : to_commit) {
        CommitNow(s);
      }
      for (auto* s : to_unpause) {
        UnpauseNow(s);
      }
      if (!by_easy.empty()) {
        int running = 0;
        curl_multi_perform(multi, &running);
        DrainMessages();
        long poll_ms = -1;
        curl_multi_timeout(multi, &poll_ms);
        if (poll_ms < 0 || poll_ms > 1000) {
          poll_ms = 1000;
        }
        if (poll_ms == 0) {
          poll_ms = 1;
        }
        curl_multi_poll(multi, nullptr, 0, static_cast<int>(poll_ms), nullptr);
      }
      lock.lock();
    }
  }

  static bool HasPendingAction(const Stream& s) {
    return s.cancel_requested || (s.commit_requested && s.easy == nullptr) || s.unpause_requested;
  }

  bool HasPendingWorkLocked() const {
    for (const auto& entry : streams) {
      if (HasPendingAction(*entry.second)) {
        return true;
      }
    }
    return false;
  }

  void CancelNow(Stream* s) {
    DeliverClosed(s, WebError{WebErrorCode::CANCELLED, "cancelled"});
    DestroyStream(s);
  }

  void UnpauseNow(Stream* s) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      s->unpause_requested = false;
    }
    if (s->easy != nullptr) {
      curl_easy_pause(s->easy, CURLPAUSE_CONT);
    }
  }

  void CommitNow(Stream* s) {
    CURL* easy = curl_easy_init();
    if (easy == nullptr) {
      DeliverClosed(s, WebError{WebErrorCode::TRANSPORT_ERROR, "curl_easy_init failed"});
      DestroyStream(s);
      return;
    }

    uint64_t total_bytes = 0;
    bool send_closed = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      s->commit_requested = false;
      s->easy = easy;
      total_bytes = s->total_bytes;
      send_closed = s->send_closed;
    }

    const std::string url = BuildUrl(base_url, s->path);
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, s->method.c_str());
    // Quick exit from waiting when the handle is closed: without it a long DNS resolve holds curl_multi_perform
    // longer than the Shutdown contract allows. The option is available since 7.87.0, the minimum version the module
    // builds with (find_package(CURL 7.87)).
    curl_easy_setopt(easy, CURLOPT_QUICK_EXIT, 1L);
    // CUSTOMREQUEST does not inherit the HEAD semantics: curl would wait for a body according to the Content-Length
    // of the response, a real server does not send one, and the stream would hang until the timeout.
    if (s->method == "HEAD") {
      curl_easy_setopt(easy, CURLOPT_NOBODY, 1L);
    }
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, s->error);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &Impl::HeaderCb);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, s);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &Impl::WriteCb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, s);
    if (options.timeout_ms > 0) {
      curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(options.timeout_ms));
    }
    if (options.max_send_speed_bytes_per_sec > 0) {
      curl_easy_setopt(
        easy, CURLOPT_MAX_SEND_SPEED_LARGE, static_cast<curl_off_t>(options.max_send_speed_bytes_per_sec)
      );
    }
    if (options.max_recv_speed_bytes_per_sec > 0) {
      curl_easy_setopt(
        easy, CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(options.max_recv_speed_bytes_per_sec)
      );
    }
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options.connect_timeout_ms));
    if (options.bypass_proxy) {
      curl_easy_setopt(easy, CURLOPT_NOPROXY, "*");
    }
    if (!options.ca_info.empty()) {
      curl_easy_setopt(easy, CURLOPT_CAINFO, options.ca_info.c_str());
    }
    if (options.debug) {
      curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
      curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, &RedactingDebugCb);
    }
    if (options.aws_sigv4) {
      const std::string sigv4 = "aws:amz:" + options.aws_sigv4->region + ":s3";
      curl_easy_setopt(easy, CURLOPT_AWS_SIGV4, sigv4.c_str());
      // USERNAME/PASSWORD rather than USERPWD: the secret may contain ':'.
      curl_easy_setopt(easy, CURLOPT_USERNAME, options.aws_sigv4->access_key.c_str());
      curl_easy_setopt(easy, CURLOPT_PASSWORD, options.aws_sigv4->secret_key.c_str());
      // S3 requires x-amz-content-sha256 in every SigV4 request; whether curl adds it automatically depends on the
      // version, so it is set here (the body is not part of the signature). A header supplied by the caller is not
      // duplicated: the curl signature takes the first match, and S3 rejects a duplicate.
      if (!HasHeader(s->headers, "x-amz-content-sha256")) {
        s->header_list = curl_slist_append(s->header_list, "x-amz-content-sha256: UNSIGNED-PAYLOAD");
      }
    }

    // An empty Expect disables the automatic 100-continue: curl would wait up to a second for "100 Continue" before
    // the body, and not every peer sends it. When the caller supplies its own Expect, the empty one is not added:
    // curl takes the first matching header, and the empty one would cancel the 100-continue wait the caller
    // explicitly asked for.
    if (!HasHeader(s->headers, "Expect")) {
      s->header_list = curl_slist_append(s->header_list, "Expect:");
    }
    for (const auto& header : s->headers) {
      s->header_list = curl_slist_append(s->header_list, (header.first + ": " + header.second).c_str());
    }
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, s->header_list);

    // UPLOAD is not set at all for methods without a body (a second guard next to the frame rejection in SendFrame).
    bool has_body = HasRequestBody(s->method);
    if (has_body && s->method == "GET" && send_closed && total_bytes == 0) {
      has_body = false;
    }
    if (has_body) {
      curl_easy_setopt(easy, CURLOPT_UPLOAD, 1L);
      curl_easy_setopt(easy, CURLOPT_READFUNCTION, &Impl::ReadCb);
      curl_easy_setopt(easy, CURLOPT_READDATA, s);
      if (send_closed) {
        curl_easy_setopt(easy, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(total_bytes));
      }
    }

    by_easy[easy] = s;
    if (curl_multi_add_handle(multi, easy) != CURLM_OK) {
      FinishStream(s, CURLE_FAILED_INIT);
    }
  }

  void DrainMessages() {
    CURLMsg* msg = nullptr;
    int left = 0;
    while ((msg = curl_multi_info_read(multi, &left)) != nullptr) {
      if (msg->msg != CURLMSG_DONE) {
        continue;
      }
      const auto it = by_easy.find(msg->easy_handle);
      if (it == by_easy.end()) {
        continue;
      }
      FinishStream(it->second, msg->data.result);
    }
  }

  void FinishStream(Stream* s, CURLcode result) {
    FetchStatusIfUnknown(s);
    // The head is normally delivered at the end of the header block; this is a fallback for the case when the header
    // callback never fired.
    DeliverHeadIfPending(s);
    WebError error;
    if (result == CURLE_OPERATION_TIMEDOUT) {
      error.code = WebErrorCode::TIMEOUT;
      error.message = s->error[0] != '\0' ? s->error : curl_easy_strerror(result);
    } else if (result != CURLE_OK) {
      error.code = WebErrorCode::TRANSPORT_ERROR;
      error.message = s->error[0] != '\0' ? s->error : curl_easy_strerror(result);
    } else if (s->http_status == 401 || s->http_status == 403) {
      error.code = WebErrorCode::AUTH_ERROR;
      error.message = "HTTP " + std::to_string(s->http_status) + ": " + s->error_prefix;
    } else if (s->http_status >= 400) {
      error.code = WebErrorCode::HTTP_ERROR;
      error.message = "HTTP " + std::to_string(s->http_status) + ": " + s->error_prefix;
    } else if (s->http_status >= 300) {
      // Redirects are disabled for the sake of SigV4: a 3xx from S3 means a wrong endpoint or region. An empty
      // WebError here would mean "delivered" although the data never arrived.
      error.code = WebErrorCode::HTTP_ERROR;
      error.message = "HTTP " + std::to_string(s->http_status) + ": unexpected redirect (redirects are disabled)";
      if (!s->error_prefix.empty()) { // 304 and HEAD+3xx carry no body: no dangling tail
        error.message += ": " + s->error_prefix;
      }
    }
    if (!error) {
      // Contract: the last frame of the response is marked end_of_stream (decoders such as Echo complete the tail
      // on that flag). WriteCb does not know which frame is the final one, so a successful close delivers an empty
      // eos frame before OnStreamClosed.
      WebFrame eos;
      eos.end_of_stream = true;
      DeliverFrame(s, std::move(eos));
    }
    DeliverClosed(s, error);
    DestroyStream(s);
  }

  // 1xx is an interim code: it must not be cached as the response; the final status arrives later, with the next
  // block.
  static void FetchStatusIfUnknown(Stream* s) {
    if (s->http_status < 200) {
      long status = 0;
      curl_easy_getinfo(s->easy, CURLINFO_RESPONSE_CODE, &status);
      s->http_status = status;
    }
  }

  // Observer callbacks are user code: an exception must not kill the worker thread.
  template<class Call>
  void InvokeObserver(const char* callback, Call&& call) {
    if (auto observer = ObserverSnapshot()) {
      try {
        call(*observer);
      } catch (const std::exception& e) {
        std::cerr << "CurlWebTransport: observer threw in " << callback << ": " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "CurlWebTransport: observer threw in " << callback << std::endl;
      }
    }
  }

  void DeliverFrame(Stream* s, WebFrame frame) {
    InvokeObserver("OnFrame", [&](web::IWebTransportObserver& observer) {
      observer.OnFrame(s->id, std::move(frame));
    });
  }

  void DeliverHeadIfPending(Stream* s) {
    if (s->response_head_sent) {
      return;
    }
    FetchStatusIfUnknown(s);
    // A status below 200 means there is no response yet: a 1xx block is interim, and 0 means there was no status
    // line at all.
    if (s->http_status < 200) {
      return;
    }
    s->response_head_sent = true;
    WebResponseHead head;
    head.status = static_cast<int32_t>(s->http_status);
    // libcurl collects and parses the headers: values are unfolded (obs-fold), while trailers and 1xx blocks live
    // under separate origins and are not included in CURLH_HEADER.
    for (struct curl_header* header = curl_easy_nextheader(s->easy, CURLH_HEADER, -1, nullptr); header != nullptr;
         header = curl_easy_nextheader(s->easy, CURLH_HEADER, -1, header)) {
      head.headers.emplace_back(header->name, header->value);
    }
    InvokeObserver("OnResponseHead", [&](web::IWebTransportObserver& observer) {
      observer.OnResponseHead(s->id, std::move(head));
    });
  }

  void DeliverClosed(Stream* s, WebError error) {
    InvokeObserver("OnStreamClosed", [&](web::IWebTransportObserver& observer) {
      observer.OnStreamClosed(s->id, std::move(error));
    });
  }

  std::shared_ptr<web::IWebTransportObserver> ObserverSnapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return observer_weak.lock();
  }

  void DestroyStream(Stream* s) {
    if (s->easy != nullptr) {
      by_easy.erase(s->easy);
      curl_multi_remove_handle(multi, s->easy);
      curl_easy_cleanup(s->easy);
      curl_slist_free_all(s->header_list);
    }
    std::lock_guard<std::mutex> lock(mutex);
    // Frames that arrived during the terminal callback are lost (the map erase after the callback is the price of
    // reentrancy); this is at least reported instead of being dropped silently.
    if (!s->frames.empty()) {
      std::cerr << "CurlWebTransport: " << s->frames.size() << " frame(s) dropped on closed stream " << s->id
                << std::endl;
    }
    s->easy = nullptr;
    streams.erase(s->id);
  }

  // The body is sent under the Impl lock, so it blocks the other API calls of the transport: one lock is easy to
  // reason about and is enough while the parallel streams are few.
  static size_t ReadCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<Stream*>(userdata);
    std::lock_guard<std::mutex> lock(s->impl->mutex);
    const size_t want = size * nmemb;
    size_t copied = 0;
    while (copied < want && !s->frames.empty()) {
      const auto& frame = *s->frames.front();
      const size_t left = frame.bytes.size() - s->frame_offset;
      const size_t take = std::min(want - copied, left);
      std::copy(frame.bytes.data() + s->frame_offset, frame.bytes.data() + s->frame_offset + take, ptr + copied);
      s->frame_offset += take;
      copied += take;
      if (s->frame_offset == frame.bytes.size()) {
        s->frames.pop_front();
        s->frame_offset = 0;
      }
    }
    if (copied > 0) {
      return copied;
    }
    if (s->send_closed) {
      return 0;
    }
    return CURL_READFUNC_PAUSE;
  }

  // Every header block ends with an empty line, an interim one included; DeliverHeadIfPending tells them apart by
  // the status. The head is delivered here rather than with the first body byte: an SSE server sends the head and
  // then pauses until the first event.
  static size_t HeaderCb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* s = static_cast<Stream*>(userdata);
    const size_t bytes = size * nitems;
    // The end of a block is an empty line; the line arrives with its terminator, and curl accepts LF, CR and CRLF,
    // hence all three forms.
    const std::string_view line(buffer, bytes);
    if (line == "\n" || line == "\r" || line == "\r\n") {
      s->impl->DeliverHeadIfPending(s);
    }
    return bytes;
  }

  static size_t WriteCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<Stream*>(userdata);
    FetchStatusIfUnknown(s);
    s->impl->DeliverHeadIfPending(s);
    const size_t bytes = size * nmemb;
    // 3xx is classified as an error (redirects are disabled for the sake of SigV4): its body is error diagnostics
    // (in a 301, S3 names the right endpoint), not payload.
    if (s->http_status >= 300) {
      if (s->error_prefix.size() < kErrorBodyCap) {
        const size_t room = kErrorBodyCap - s->error_prefix.size();
        s->error_prefix.append(ptr, ptr + std::min(bytes, room));
      }
      return bytes;
    }
    WebFrame frame;
    frame.bytes.assign(ptr, ptr + bytes);
    s->impl->DeliverFrame(s, std::move(frame));
    return bytes;
  }

  static std::string BuildUrl(const std::string& base, const std::string& path) {
    if (path.empty()) {
      return base;
    }
    const size_t scheme_end = base.find("://");
    const size_t authority_end = scheme_end == std::string::npos ? std::string::npos : base.find('/', scheme_end + 3);
    const std::string origin = authority_end == std::string::npos ? base : base.substr(0, authority_end);
    return origin + path;
  }

  const std::string base_url;
  const CurlWebTransportOptions options;

  mutable std::mutex mutex;
  std::weak_ptr<web::IWebTransportObserver> observer_weak;
  std::map<WebStreamId, std::unique_ptr<Stream>> streams;
  WebStreamId next_id = 1;

  CURLM* multi = nullptr;                     // used by the worker thread; API threads only call curl_multi_wakeup
  std::unordered_map<CURL*, Stream*> by_easy; // used only by the worker thread
  // worker_id is written once when the thread starts and is read under mutex; the default id ("the thread has not
  // started") equals the id of no thread. The thread is detached: its exit is marked by the worker_done flag (under
  // mutex, with cv as the wakeup).
  std::thread::id worker_id;
  bool worker_done = false;
  std::condition_variable cv;
  bool stopping = false;
};

CurlWebTransport::CurlWebTransport(std::string base_url, CurlWebTransportOptions options) {
  if (base_url.rfind("http://", 0) != 0 && base_url.rfind("https://", 0) != 0) {
    throw std::runtime_error("CurlWebTransport: base_url must start with http:// or https://");
  }
  if (options.aws_sigv4) {
    const auto& sigv4 = *options.aws_sigv4;
    if (sigv4.region.empty() || sigv4.access_key.empty() || sigv4.secret_key.empty()) {
      throw std::runtime_error("CurlWebTransport: aws_sigv4 requires region, access_key and secret_key");
    }
  }
  impl_ = std::make_shared<Impl>(std::move(base_url), std::move(options));
}

// The stop signal must be sent while the wrapper still holds impl_: ~Impl runs only after the last reference is
// released (including the worker's self-hold), so without an explicit Shutdown here the worker would not exit and
// Impl would leak.
CurlWebTransport::~CurlWebTransport() {
  impl_->Shutdown();
}

void CurlWebTransport::SetObserver(std::weak_ptr<web::IWebTransportObserver> observer) {
  impl_->SetObserver(std::move(observer));
}

WebStreamId CurlWebTransport::OpenStream(const WebStreamDescriptor& descriptor) {
  return impl_->OpenStream(descriptor);
}

bool CurlWebTransport::SendFrame(WebStreamId id, WebFrame frame) {
  return impl_->SendFrame(id, std::move(frame));
}

bool CurlWebTransport::SendFrameShared(WebStreamId id, std::shared_ptr<const WebFrame> frame) {
  return impl_->SendFrameShared(id, std::move(frame));
}

void CurlWebTransport::CloseSend(WebStreamId id) {
  impl_->CloseSend(id);
}

void CurlWebTransport::CloseStream(WebStreamId id) {
  impl_->CloseStream(id);
}

void CurlWebTransport::Shutdown() noexcept {
  impl_->Shutdown();
}

std::shared_ptr<CurlWebTransport> CreateCurlWebTransport(std::string base_url, CurlWebTransportOptions options) {
  return std::make_shared<CurlWebTransport>(std::move(base_url), std::move(options));
}

} // namespace web_transport_curl
} // namespace giga_edge
