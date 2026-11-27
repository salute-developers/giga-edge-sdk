#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/assistant_core/prompt_template.h"
#include "giga_edge/modules/llm/interpreter/interpreter.h"
#include "giga_edge/modules/llm/kv_transcript.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

struct PromptPlan {
  std::string prompt;
  std::string tail;
  size_t keep_tokens = 0;
  std::vector<LlmToken> tail_tokens;
  size_t dropped_turns = 0;
};

class PromptPlanner {
public:
  using Tokenizer = KvTranscript::Tokenizer;
  using IsBoundary = KvTranscript::IsBoundary;
  // The texts of the vocabulary's special tokens: their occurrences in foreign prompt text are escaped.
  using SpecialTexts = std::function<const std::vector<std::string>&()>;

  // Without is_boundary the whole prompt is tokenized: correct but slower.
  PromptPlanner(
    const PromptTemplate& prompt_template,
    Tokenizer tokenizer,
    IsBoundary is_boundary = {},
    SpecialTexts special_texts = {}
  );

  std::optional<PromptPlan> Plan(
    AssistantRequest request,
    const KvTranscript& kv,
    size_t capacity,
    size_t reserve,
    size_t min_tail,
    std::string& error
  ) const;

  static bool DropOldestTurn(AssistantRequest& request);

private:
  const PromptTemplate& template_;
  Tokenizer tokenizer_;
  IsBoundary is_boundary_;
  SpecialTexts special_texts_;
};

} // namespace giga_edge
