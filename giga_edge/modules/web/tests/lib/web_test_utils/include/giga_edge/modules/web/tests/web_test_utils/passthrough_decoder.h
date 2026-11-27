#pragma once

#include "giga_edge/modules/web/web_decoder.h"

#include <mutex>
#include <string>
#include <vector>

namespace giga_edge {
namespace web {
namespace test_utils {

class PassthroughDecoder : public IWebResponseDecoder {
public:
  std::vector<WebDecodedChunk> DecodeFrame(WebStreamId /*id*/, const WebFrame& frame) override {
    return {MakeTextChunk(std::string(frame.bytes.begin(), frame.bytes.end()))};
  }

  std::vector<WebDecodedChunk> Finalize(WebStreamId /*id*/, const WebError& error) override {
    return {MakeTerminalChunk(error)};
  }
};

} // namespace test_utils
} // namespace web
} // namespace giga_edge
