#pragma once

#include "giga_edge/modules/web/web_decoder.h"
#include "giga_edge/modules/web/web_transport.h"
#include "giga_edge/modules/web/web_types.h"

#include <memory>
#include <string>
#include <vector>

namespace giga_edge {
namespace web {

class IWebServiceObserver {
public:
  virtual ~IWebServiceObserver() = default;

  virtual void OnChunk(WebRequestToken token, WebDecodedChunk chunk) = 0;
};

class IWebService {
public:
  virtual ~IWebService() = default;

  virtual void SetObserver(std::weak_ptr<IWebServiceObserver> observer) = 0;

  virtual void AddTransport(const std::string& provider_id, WebTransportPtr transport) = 0;
  virtual void RemoveTransport(const std::string& provider_id) = 0;
  virtual std::vector<std::string> GetProviderIds() const = 0;

  virtual bool SubmitAsync(
    WebRequestToken token,
    const std::string& provider_id,
    WebStreamDescriptor descriptor,
    std::vector<WebFrame> up_frames,
    WebResponseDecoderPtr decoder
  ) = 0;

  virtual bool SendFrame(WebRequestToken token, WebFrame frame) = 0;

  virtual bool CloseSend(WebRequestToken token) = 0;

  virtual bool Cancel(WebRequestToken token) = 0;

  // Stops the service threads and closes its streams. Called by the single owner; a repeated call is safe.
  // Must not be called from service callbacks (observer, transport): Shutdown waits for the threads they run on.
  virtual void Shutdown() = 0;
};

// The last reference may be released from any thread, including service callbacks: in that case the destruction is
// asynchronous, on a separate thread.
std::shared_ptr<IWebService> CreateWebService(WebServiceConfig config);

} // namespace web
} // namespace giga_edge
