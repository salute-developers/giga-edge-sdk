#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge {

using LlmToken = int32_t;

struct KvReusePlan {
  size_t keep_tokens = 0;
  size_t keep_bytes = 0;
  std::string tail;
  std::vector<LlmToken> tail_tokens;
};

class KvTranscript {
public:
  using Tokenizer = std::function<std::optional<std::vector<LlmToken>>(const std::string& text, bool prepend_bos)>;
  using IsBoundary = std::function<bool(LlmToken token)>;

  // The prompt tail is tokenized from the end of the last special token in the part shared with the cache: the
  // tokenizer splits the text at special tokens, so this way the tail is tokenized exactly as in the whole prompt.
  // Cutting by bytes is not allowed: the tail comes out non-canonical ("v|yklyuchi" instead of "vy|klyuchi"). Tail
  // tokens that match the cache are reused too. Without special tokens the whole prompt is tokenized.
  // min_tail: how many last prompt tokens to recompute; the head needs the states of several, not only of the last.
  std::optional<KvReusePlan> Plan(
    std::string_view prompt, const Tokenizer& tokenize, const IsBoundary& is_boundary, size_t min_tail = 0
  ) const;

  void Append(std::string_view text, const std::vector<uint32_t>& token_ends, const std::vector<LlmToken>& tokens);
  void Truncate(size_t token_count);
  void Clear();

  size_t Tokens() const {
    return tokens_.size();
  }
  const std::string& Text() const {
    return text_;
  }

private:
  // Whether a cut after the first count tokens is allowed: their end is reliable and matches the prompt.
  bool CanCut(size_t count, std::string_view prompt) const;
  // The last cut point after a special token, no further than first_after - 1 tokens and the shared part.
  size_t Anchor(size_t first_after, size_t common, std::string_view prompt, const IsBoundary& is_boundary) const;

  std::string text_;
  std::vector<uint32_t> token_ends_;
  std::vector<LlmToken> tokens_;
};

} // namespace giga_edge
