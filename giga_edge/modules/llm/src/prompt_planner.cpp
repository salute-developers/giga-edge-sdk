#include "giga_edge/modules/llm/prompt_planner.h"

#include "giga_edge/modules/llm/special_text_escape.h"

namespace giga_edge {

namespace {

bool NoBoundary(LlmToken) {
  return false;
}

const std::vector<std::string>& NoSpecialTexts() {
  static const std::vector<std::string> none;
  return none;
}

} // namespace

PromptPlanner::PromptPlanner(
  const PromptTemplate& prompt_template, Tokenizer tokenizer, IsBoundary is_boundary, SpecialTexts special_texts
)
  : template_(prompt_template)
  , tokenizer_(std::move(tokenizer))
  , is_boundary_(is_boundary ? std::move(is_boundary) : IsBoundary(NoBoundary))
  , special_texts_(special_texts ? std::move(special_texts) : SpecialTexts(NoSpecialTexts)) {}

bool PromptPlanner::DropOldestTurn(AssistantRequest& request) {
  auto& messages = request.messages;
  size_t first = messages.size();
  for (size_t i = 0; i < messages.size(); ++i) {
    if (messages[i].role == MessageRole::USER) {
      first = i;
      break;
    }
  }
  if (first == messages.size()) {
    return false;
  }
  size_t next = messages.size();
  for (size_t i = first + 1; i < messages.size(); ++i) {
    if (messages[i].role == MessageRole::USER) {
      next = i;
      break;
    }
  }
  if (next == messages.size()) {
    return false;
  }
  messages.erase(
    messages.begin() + static_cast<std::ptrdiff_t>(first), messages.begin() + static_cast<std::ptrdiff_t>(next)
  );
  return true;
}

std::optional<PromptPlan> PromptPlanner::Plan(
  AssistantRequest request, const KvTranscript& kv, size_t capacity, size_t reserve, size_t min_tail, std::string& error
) const {
  PromptPlan plan;
  while (true) {
    const auto rendered = template_.RenderMarked(request);
    plan.prompt = EscapeSpecialTexts(rendered.text, rendered.spans, special_texts_());
    auto reuse = kv.Plan(plan.prompt, tokenizer_, is_boundary_, min_tail);
    if (!reuse.has_value()) {
      error = "LLM backend does not support tokenization.";
      return std::nullopt;
    }
    if (capacity == 0 || reuse->keep_tokens + reuse->tail_tokens.size() + reserve <= capacity) {
      plan.tail = std::move(reuse->tail);
      plan.keep_tokens = reuse->keep_tokens;
      plan.tail_tokens = std::move(reuse->tail_tokens);
      return plan;
    }
    if (!DropOldestTurn(request)) {
      error = "LLM context overflow: reused=" + std::to_string(reuse->keep_tokens)
        + " tail=" + std::to_string(reuse->tail_tokens.size()) + " reserved=" + std::to_string(reserve)
        + " capacity=" + std::to_string(capacity);
      return std::nullopt;
    }
    ++plan.dropped_turns;
  }
}

} // namespace giga_edge
