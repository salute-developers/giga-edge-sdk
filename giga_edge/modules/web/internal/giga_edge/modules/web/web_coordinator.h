#pragma once

#include "giga_edge/modules/web/web_decoder.h"
#include "giga_edge/modules/web/web_service.h"
#include "giga_edge/modules/web/web_transport.h"
#include "giga_edge/modules/web/web_types.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace giga_edge {
namespace web {

class IWebCoordinator {
public:
  virtual ~IWebCoordinator() = default;

  virtual WebConsumerId RegisterConsumer(WebConsumerConfig config) = 0;
  virtual WebConsumerId EnsureConsumer(WebConsumerConfig config) = 0;
  virtual void UnregisterConsumer(WebConsumerId consumer_id) = 0;

  virtual SubmitResult Submit(
    WebConsumerId consumer_id,
    const std::string& provider_id,
    WebStreamDescriptor descriptor,
    std::vector<WebFrame> up_frames,
    WebResponseDecoderPtr decoder
  ) = 0;

  virtual bool SendFrame(WebConsumerId consumer_id, WebRequestToken token, WebFrame frame) = 0;
  virtual bool CloseSend(WebConsumerId consumer_id, WebRequestToken token) = 0;

  virtual bool CancelRequest(WebConsumerId consumer_id, WebRequestToken token) = 0;

  virtual TakeBatchResult<WebDecodedChunk> TakeChunkResults(
    WebConsumerId consumer_id, WebRequestToken token, size_t limit
  ) = 0;

  virtual bool HasRequest(WebConsumerId consumer_id, WebRequestToken token) const = 0;
};

std::shared_ptr<IWebCoordinator> CreateWebCoordinator(std::shared_ptr<IWebService> service);

} // namespace web
} // namespace giga_edge
