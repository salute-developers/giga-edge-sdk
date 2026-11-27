#include "giga_edge/modules/llm/kv_transcript.h"

#include <algorithm>
#include <stdexcept>

namespace giga_edge {

bool KvTranscript::CanCut(size_t count, std::string_view prompt) const {
  if (count == 0) {
    return true;
  }
  const size_t end = token_ends_[count - 1];
  // In a piece whose tokens did not add up to its text all ends sit at the end of the piece: it cannot be cut inside.
  const bool exact = count == token_ends_.size() || end < token_ends_[count];
  return exact && end <= prompt.size() && prompt.compare(0, end, text_, 0, end) == 0;
}

size_t KvTranscript::Anchor(
  size_t first_after, size_t common, std::string_view prompt, const IsBoundary& is_boundary
) const {
  for (size_t count = std::min(first_after, tokens_.size() + 1); count > 1; --count) {
    const size_t i = count - 1;
    if (token_ends_[i - 1] <= common && is_boundary(tokens_[i - 1]) && CanCut(i, prompt)) {
      return i;
    }
  }
  return 0;
}

std::optional<KvReusePlan> KvTranscript::Plan(
  std::string_view prompt, const Tokenizer& tokenize, const IsBoundary& is_boundary, size_t min_tail
) const {
  size_t common = 0;
  const size_t limit = std::min(text_.size(), prompt.size());
  while (common < limit && text_[common] == prompt[common]) {
    ++common;
  }

  size_t anchor = Anchor(tokens_.size() + 1, common, prompt, is_boundary);
  while (true) {
    const size_t bytes = anchor == 0 ? 0 : token_ends_[anchor - 1];
    const auto tail_tokens = tokenize(std::string(prompt.substr(bytes)), anchor == 0);
    if (!tail_tokens.has_value()) {
      return std::nullopt;
    }
    // The tokenizer decides about the tail; its tokens that match the cache are not recomputed.
    size_t keep = anchor;
    size_t used = 0;
    while (keep < tokens_.size() && used < tail_tokens->size() && tokens_[keep] == (*tail_tokens)[used]) {
      ++keep;
      ++used;
    }
    while (keep > anchor && !CanCut(keep, prompt)) {
      --keep;
      --used;
    }
    while (used > 0 && tail_tokens->size() - used < min_tail) {
      --keep;
      --used;
      while (keep > anchor && !CanCut(keep, prompt)) {
        --keep;
        --used;
      }
    }
    if (tail_tokens->size() < min_tail && anchor > 0) {
      anchor = Anchor(anchor, common, prompt, is_boundary);
      continue;
    }

    KvReusePlan plan;
    plan.keep_tokens = keep;
    plan.keep_bytes = keep == 0 ? 0 : token_ends_[keep - 1];
    plan.tail = std::string(prompt.substr(plan.keep_bytes));
    plan.tail_tokens.assign(tail_tokens->begin() + static_cast<std::ptrdiff_t>(used), tail_tokens->end());
    return plan;
  }
}

void KvTranscript::Append(
  std::string_view text, const std::vector<uint32_t>& token_ends, const std::vector<LlmToken>& tokens
) {
  if (token_ends.size() != tokens.size()) {
    throw std::logic_error("KvTranscript: token ends do not match tokens");
  }
  if (!token_ends.empty() && token_ends.back() != text.size()) {
    throw std::logic_error("KvTranscript: token ends do not cover the appended text");
  }
  const uint32_t base = static_cast<uint32_t>(text_.size());
  text_.append(text.data(), text.size());
  for (const auto end : token_ends) {
    token_ends_.push_back(base + end);
  }
  tokens_.insert(tokens_.end(), tokens.begin(), tokens.end());
}

void KvTranscript::Truncate(size_t token_count) {
  if (token_count >= tokens_.size()) {
    return;
  }
  const size_t bytes = token_count == 0 ? 0 : token_ends_[token_count - 1];
  token_ends_.resize(token_count);
  tokens_.resize(token_count);
  text_.resize(bytes);
}

void KvTranscript::Clear() {
  text_.clear();
  token_ends_.clear();
  tokens_.clear();
}

} // namespace giga_edge
