#pragma once

#include "giga_edge/modules/web/web_transport.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace giga_edge {
namespace web {

enum class WebFinishReason {
  NONE,
  COMPLETED,
  MAX_TOKENS,
  CANCELLED,
  ERROR,
};

struct WebChunkMeta {
  bool is_final = false;
  WebFinishReason finish_reason = WebFinishReason::NONE;
  bool is_error = false;
  std::string error;
};

struct WebTextChunk {
  std::string text;
  bool is_complete = false;
};

struct WebBinaryChunk {
  std::vector<char> bytes;
  std::string content_type;
};

struct WebToolCallChunk {
  std::string id;
  std::string name;
  std::string arguments_json;
};

using WebChunkPayload = std::variant<std::monostate, WebTextChunk, WebBinaryChunk, WebToolCallChunk>;

struct WebDecodedChunk {
  WebChunkMeta meta;
  WebChunkPayload payload;
};

template<class T>
const T* ChunkPayloadAs(const WebDecodedChunk& chunk) {
  return std::get_if<T>(&chunk.payload);
}

inline WebDecodedChunk MakeTextChunk(std::string text, bool is_complete = false) {
  WebDecodedChunk chunk;
  chunk.payload = WebTextChunk{std::move(text), is_complete};
  return chunk;
}

inline WebDecodedChunk MakeBinaryChunk(std::vector<char> bytes, std::string content_type = {}) {
  WebDecodedChunk chunk;
  chunk.payload = WebBinaryChunk{std::move(bytes), std::move(content_type)};
  return chunk;
}

inline WebDecodedChunk MakeToolCallChunk(std::string name, std::string arguments_json, std::string id = {}) {
  WebDecodedChunk chunk;
  chunk.payload = WebToolCallChunk{std::move(id), std::move(name), std::move(arguments_json)};
  return chunk;
}

inline WebDecodedChunk MakeErrorChunk(std::string error) {
  WebDecodedChunk chunk;
  chunk.meta.is_final = true;
  chunk.meta.finish_reason = WebFinishReason::ERROR;
  chunk.meta.is_error = true;
  chunk.meta.error = std::move(error);
  return chunk;
}

inline WebDecodedChunk MakeTerminalChunk(const WebError& error) {
  if (!error) {
    WebDecodedChunk chunk;
    chunk.meta.is_final = true;
    chunk.meta.finish_reason = WebFinishReason::COMPLETED;
    return chunk;
  }
  auto chunk = MakeErrorChunk(error.message);
  if (error.code == WebErrorCode::CANCELLED) {
    chunk.meta.finish_reason = WebFinishReason::CANCELLED;
  }
  return chunk;
}

class IWebResponseDecoder {
public:
  virtual ~IWebResponseDecoder() = default;

  virtual std::vector<WebDecodedChunk> DecodeFrame(WebStreamId id, const WebFrame& frame) = 0;
  virtual std::vector<WebDecodedChunk> Finalize(WebStreamId id, const WebError& error) = 0;
};

using WebResponseDecoderPtr = std::shared_ptr<IWebResponseDecoder>;

} // namespace web
} // namespace giga_edge
