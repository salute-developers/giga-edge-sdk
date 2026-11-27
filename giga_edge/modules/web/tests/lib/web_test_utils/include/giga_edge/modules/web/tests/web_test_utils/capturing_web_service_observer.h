#pragma once

#include "giga_edge/modules/web/tests/web_test_utils/wait_for.h"
#include "giga_edge/modules/web/web_service.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace giga_edge {
namespace web {
namespace test_utils {

class CapturingWebServiceObserver : public IWebServiceObserver {
public:
  void OnChunk(WebRequestToken token, WebDecodedChunk chunk) override {
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_[token].push_back(std::move(chunk));
  }

  std::vector<WebDecodedChunk> Chunks(WebRequestToken token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = chunks_.find(token);
    return it == chunks_.end() ? std::vector<WebDecodedChunk>{} : it->second;
  }

  size_t ChunkCount(WebRequestToken token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = chunks_.find(token);
    return it == chunks_.end() ? 0 : it->second.size();
  }

  std::string Text(WebRequestToken token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    auto it = chunks_.find(token);
    if (it != chunks_.end()) {
      for (const auto& chunk : it->second) {
        if (const auto* text = ChunkPayloadAs<WebTextChunk>(chunk)) {
          out += text->text;
        }
      }
    }
    return out;
  }

  bool HasFinal(WebRequestToken token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = chunks_.find(token);
    if (it == chunks_.end()) {
      return false;
    }
    for (const auto& chunk : it->second) {
      if (chunk.meta.is_final) {
        return true;
      }
    }
    return false;
  }

private:
  mutable std::mutex mutex_;
  std::map<WebRequestToken, std::vector<WebDecodedChunk>> chunks_;
};

} // namespace test_utils
} // namespace web
} // namespace giga_edge
