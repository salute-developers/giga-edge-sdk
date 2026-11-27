#pragma once

#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace giga_edge {

class ILlmInterpreter;

// llama.cpp writes only errors and warnings to stderr, the verbose loading log stays silent.
void ConfigureLlamaLogging();

// Where the accepted speculation drafts come from: the MTP layers of the model or the text already in the context.
enum class DraftSource {
  MTP,
  PROMPT_LOOKUP,
};

class LlmLocalDebugSession {
public:
  explicit LlmLocalDebugSession(
    bool enabled = false, bool content = false, DraftSource drafts = DraftSource::PROMPT_LOOKUP
  )
    : enabled_(enabled)
    , content_(content)
    , drafts_(drafts) {}

  bool Enabled() const {
    return enabled_;
  }
  void Reset();

  void StartPrefill(ILlmInterpreter& interpreter, const std::string& prompt_text);
  void LogPrefillFailed(const std::string& error = "") const;
  void LogPrefillFinished() const;

  void LogPrefillJobStarted(
    const std::string& kind, const std::string& id, size_t total_tokens, size_t tokens_to_append, size_t chunk_tokens
  );
  void LogPrefillChunkFinished(
    const std::string& kind,
    const std::string& id,
    size_t offset_tokens,
    size_t chunk_tokens,
    size_t context_tokens,
    double elapsed_ms
  ) const;
  void LogPrefillJobFinished(
    const std::string& kind, const std::string& id, size_t total_tokens, size_t chunks, size_t context_tokens
  ) const;

  void LogPromptHead(
    const std::vector<std::string>& labels,
    const std::vector<float>& probabilities,
    const std::string& label,
    bool declined
  ) const;

  void MarkGenerationStarted();
  void LogGenerationFinished(
    StreamFinishReason finish_reason,
    AssistantStatus status,
    int generated_tokens_count,
    size_t accepted_drafts,
    const std::string& error = ""
  ) const;

  // After generation: optional KV tail from `stop_sequence` (EOS vs reported finish).
  // The generated text preview is written only when the session was created with content dumps on.
  void LogGenerationStopTail(
    StreamFinishReason raw_finish_reason,
    StreamFinishReason reported_finish_reason,
    size_t appended_stop_tokens,
    const std::string& active_generated_text
  ) const;

private:
  bool enabled_ = false;
  bool content_ = false;
  DraftSource drafts_ = DraftSource::PROMPT_LOOKUP;
  std::optional<std::chrono::steady_clock::time_point> prefill_started_at_;
  std::optional<std::chrono::steady_clock::time_point> generation_started_at_;
  size_t prompt_bytes_ = 0;
  std::optional<size_t> prompt_tokens_;
};

} // namespace giga_edge
