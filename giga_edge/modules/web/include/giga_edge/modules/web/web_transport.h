#pragma once

#include "giga_edge/lib/common/shutdown.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace giga_edge {
namespace web {

enum class WebErrorCode : int32_t {
  NONE = 0,
  TRANSPORT_ERROR = -1,
  TIMEOUT = -2,
  CANCELLED = -3,
  AUTH_ERROR = -4,
  HTTP_ERROR = -5,
};

struct WebError {
  WebErrorCode code = WebErrorCode::NONE;
  std::string message;

  explicit operator bool() const {
    return code != WebErrorCode::NONE;
  }
};

using WebStreamId = uint64_t;

struct WebStreamDescriptor {
  std::vector<std::pair<std::string, std::string>> headers;
  bool keep_send_open = false;
  std::optional<uint32_t> idle_timeout_ms;
};

struct WebFrame {
  std::vector<char> bytes;
  bool end_of_stream = false;
};

// Headers as the server sent them: name case and duplicates are kept (HTTP names are
// case-insensitive, parsing is up to the observer).
struct WebResponseHead {
  int32_t status = 0;
  std::vector<std::pair<std::string, std::string>> headers;
};

class IWebTransportObserver {
public:
  virtual ~IWebTransportObserver() = default;

  // At most once per stream, before the first OnFrame of the stream; delivered for any
  // received response, including statuses >=300 and responses without a body. After a break
  // before the response or a cancel before delivery the callback may not come.
  virtual void OnResponseHead(WebStreamId id, WebResponseHead head) {}

  virtual void OnFrame(WebStreamId id, WebFrame frame) = 0;
  // May come without OnResponseHead and OnFrame: an error before the response, a cancel (CloseStream), Shutdown.
  virtual void OnStreamClosed(WebStreamId id, WebError error) = 0;
};

class IWebTransport {
public:
  virtual ~IWebTransport() = default;

  virtual void SetObserver(std::weak_ptr<IWebTransportObserver> observer) = 0;

  // 0 means the stream could not be opened.
  virtual WebStreamId OpenStream(const WebStreamDescriptor& descriptor) = 0;

  // false means the frame is not accepted: the stream does not exist, its sending is already closed (CloseSend)
  // or the method allows no body (see the implementation contract).
  virtual bool SendFrame(WebStreamId id, WebFrame frame) = 0;

  virtual void CloseSend(WebStreamId id) = 0;

  virtual void CloseStream(WebStreamId id) = 0;
};

// Shutdown is idempotent: it cancels streams with OnStreamClosed(CANCELLED) and waits for the delivery threads;
// no callbacks after it returns. Do not call it from an observer callback: it would wait for its own thread.
class IWebTransportWithShutdown : public IWebTransport, public IShutdown {};

using WebTransportPtr = std::shared_ptr<IWebTransport>;

} // namespace web
} // namespace giga_edge
