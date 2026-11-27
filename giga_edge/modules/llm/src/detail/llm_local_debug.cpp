#include "giga_edge/modules/llm/detail/llm_local_debug.h"

#include "giga_edge/modules/llm/interpreter/interpreter.h"

#ifdef LLAMA_CPP_ENABLED
#include "llama.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string FormatWallClockNow() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::system_clock::to_time_t(now);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &seconds);
#else
  localtime_r(&seconds, &local_time);
#endif

  std::ostringstream stream;
  stream << std::put_time(&local_time, "%F %T") << '.' << std::setw(3) << std::setfill('0') << milliseconds.count();
  return stream.str();
}

double DurationMs(const std::chrono::steady_clock::time_point& started_at) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started_at).count();
}

void LogTimingEvent(const std::string& phase, const std::string& details) {
  std::cout << "[llm_timing] " << FormatWallClockNow() << " " << phase;
  if (!details.empty()) {
    std::cout << " | " << details;
  }
  std::cout << std::endl;
}

std::string EscapePreview(const std::string& text, size_t max_size = 96) {
  std::string preview;
  preview.reserve(std::min(text.size(), max_size) + 3);

  for (char ch : text) {
    if (preview.size() >= max_size) {
      preview += "...";
      break;
    }
    if (ch == '\n') {
      preview += "\\n";
    } else if (ch == '\r') {
      preview += "\\r";
    } else if (ch == '\t') {
      preview += "\\t";
    } else {
      preview += ch;
    }
  }

  return preview;
}

// prefill_tokens_total repeats prompt_tokens: the log format has both keys.
void AppendPrefillMetadata(
  std::ostringstream& details, size_t prompt_bytes, const std::optional<size_t>& prompt_tokens
) {
  details << " prompt_bytes=" << prompt_bytes;
  if (prompt_tokens.has_value()) {
    details << " prompt_tokens=" << *prompt_tokens << " prefill_tokens_total=" << *prompt_tokens;
  }
}

void AppendPrefillTokensPerSecond(
  std::ostringstream& details, const std::optional<size_t>& prompt_tokens, double elapsed_ms
) {
  if (!prompt_tokens.has_value() || elapsed_ms <= 0.0) {
    return;
  }

  const double tokens_per_second = static_cast<double>(*prompt_tokens) * 1000.0 / elapsed_ms;
  details << " prefill_tokens_per_second=" << std::fixed << std::setprecision(3) << tokens_per_second;
}

#ifdef LLAMA_CPP_ENABLED
struct LlamaLogRoutingState {
  std::atomic<bool> print_continuation = false;
};

void LlamaLogCallback(enum ggml_log_level level, const char* text, void* user_data) {
  auto* state = static_cast<LlamaLogRoutingState*>(user_data);
  if (state == nullptr || text == nullptr) {
    return;
  }

  const bool should_print = level == GGML_LOG_LEVEL_ERROR || level == GGML_LOG_LEVEL_WARN
    || (level == GGML_LOG_LEVEL_CONT && state->print_continuation);

  if (!should_print) {
    if (level != GGML_LOG_LEVEL_CONT) {
      state->print_continuation = false;
    }
    return;
  }

  std::fputs(text, stderr);
  std::fflush(stderr);

  if (level != GGML_LOG_LEVEL_CONT) {
    state->print_continuation = true;
  }
}
#endif

} // namespace

namespace giga_edge {

void ConfigureLlamaLogging() {
#ifdef LLAMA_CPP_ENABLED
  static LlamaLogRoutingState state;
  llama_log_set(LlamaLogCallback, &state);
#endif
}

void LlmLocalDebugSession::Reset() {
  prefill_started_at_.reset();
  generation_started_at_.reset();
  prompt_bytes_ = 0;
  prompt_tokens_.reset();
}

void LlmLocalDebugSession::StartPrefill(ILlmInterpreter& interpreter, const std::string& prompt_text) {
  Reset();

  if (!enabled_) {
    return;
  }

  prompt_bytes_ = prompt_text.size();

#ifdef LLAMA_CPP_ENABLED
  prompt_tokens_ = interpreter.GetPromptTokenCount(prompt_text);
#endif

  prefill_started_at_ = std::chrono::steady_clock::now();

  std::ostringstream details;
  AppendPrefillMetadata(details, prompt_bytes_, prompt_tokens_);
  LogTimingEvent("prefill_started", details.str());
}

void LlmLocalDebugSession::LogPrefillFailed(const std::string& error) const {
  if (!enabled_ || !prefill_started_at_.has_value()) {
    return;
  }

  const double elapsed_ms = DurationMs(*prefill_started_at_);
  std::ostringstream details;
  details << "elapsed_ms=" << std::fixed << std::setprecision(3) << elapsed_ms;
  AppendPrefillMetadata(details, prompt_bytes_, prompt_tokens_);
  AppendPrefillTokensPerSecond(details, prompt_tokens_, elapsed_ms);
  if (!error.empty()) {
    details << " error=\"" << error << '"';
  }

  LogTimingEvent("prefill_failed", details.str());
}

void LlmLocalDebugSession::LogPrefillFinished() const {
  if (!enabled_ || !prefill_started_at_.has_value()) {
    return;
  }

  const double elapsed_ms = DurationMs(*prefill_started_at_);
  std::ostringstream details;
  details << "elapsed_ms=" << std::fixed << std::setprecision(3) << elapsed_ms;
  AppendPrefillMetadata(details, prompt_bytes_, prompt_tokens_);
  AppendPrefillTokensPerSecond(details, prompt_tokens_, elapsed_ms);
  LogTimingEvent("prefill_finished", details.str());
}

void LlmLocalDebugSession::LogPrefillJobStarted(
  const std::string& kind, const std::string& id, size_t total_tokens, size_t tokens_to_append, size_t chunk_tokens
) {
  if (!enabled_) {
    return;
  }

  prefill_started_at_ = std::chrono::steady_clock::now();
  std::ostringstream details;
  details << "kind=\"" << kind << '"' << " id=\"" << id << '"' << " total_tokens=" << total_tokens
          << " tokens_to_append=" << tokens_to_append << " chunk_tokens=" << chunk_tokens;
  LogTimingEvent("prefill_job_started", details.str());
}

void LlmLocalDebugSession::LogPrefillChunkFinished(
  const std::string& kind,
  const std::string& id,
  size_t offset_tokens,
  size_t chunk_tokens,
  size_t context_tokens,
  double elapsed_ms
) const {
  if (!enabled_) {
    return;
  }

  std::ostringstream details;
  details << "kind=\"" << kind << '"' << " id=\"" << id << '"' << " offset_tokens=" << offset_tokens
          << " chunk_tokens=" << chunk_tokens << " context_tokens=" << context_tokens << " elapsed_ms=" << std::fixed
          << std::setprecision(3) << elapsed_ms;
  LogTimingEvent("prefill_chunk_finished", details.str());
}

void LlmLocalDebugSession::LogPrefillJobFinished(
  const std::string& kind, const std::string& id, size_t total_tokens, size_t chunks, size_t context_tokens
) const {
  if (!enabled_) {
    return;
  }

  std::ostringstream details;
  details << "kind=\"" << kind << '"' << " id=\"" << id << '"' << " total_tokens=" << total_tokens
          << " chunks=" << chunks << " context_tokens=" << context_tokens;
  if (prefill_started_at_.has_value()) {
    details << " elapsed_ms=" << std::fixed << std::setprecision(3) << DurationMs(*prefill_started_at_);
  }
  LogTimingEvent("prefill_job_finished", details.str());
}

void LlmLocalDebugSession::MarkGenerationStarted() {
  if (!enabled_ || generation_started_at_.has_value()) {
    return;
  }

  generation_started_at_ = std::chrono::steady_clock::now();

  std::ostringstream details;
  if (prefill_started_at_.has_value()) {
    details << "prefill_elapsed_ms=" << std::fixed << std::setprecision(3) << DurationMs(*prefill_started_at_);
  }

  LogTimingEvent("generation_started", details.str());
}

void LlmLocalDebugSession::LogGenerationFinished(
  StreamFinishReason finish_reason,
  AssistantStatus status,
  int generated_tokens_count,
  size_t accepted_drafts,
  const std::string& error
) const {
  if (!enabled_) {
    return;
  }

  std::ostringstream details;
  details << "finish_reason=" << ToString(finish_reason) << " status=" << ToString(status)
          << " generated_tokens=" << generated_tokens_count;

  if (generation_started_at_.has_value()) {
    const double generation_ms = DurationMs(*generation_started_at_);
    details << " generation_elapsed_ms=" << std::fixed << std::setprecision(3) << generation_ms;
    if (generation_ms > 0.0) {
      const double tokens_per_second = static_cast<double>(generated_tokens_count) * 1000.0 / generation_ms;
      details << " tokens_per_second=" << std::fixed << std::setprecision(3) << tokens_per_second;
    }
  }

  if (accepted_drafts > 0) {
    details << (drafts_ == DraftSource::MTP ? " mtp" : " lookup") << "_accepted_drafts=" << accepted_drafts;
  }

  if (!error.empty()) {
    details << " error=\"" << error << '"';
  }

  LogTimingEvent("generation_finished", details.str());
}

void LlmLocalDebugSession::LogPromptHead(
  const std::vector<std::string>& labels,
  const std::vector<float>& probabilities,
  const std::string& label,
  bool declined
) const {
  if (!enabled_) {
    return;
  }

  std::ostringstream details;
  details << "label=" << label << " declined=" << (declined ? "true" : "false");
  for (size_t i = 0; i < labels.size() && i < probabilities.size(); ++i) {
    details << " p_" << labels[i] << '=' << std::fixed << std::setprecision(4) << probabilities[i];
  }
  LogTimingEvent("prompt_head", details.str());
}

void LlmLocalDebugSession::LogGenerationStopTail(
  StreamFinishReason raw_finish_reason,
  StreamFinishReason reported_finish_reason,
  size_t appended_stop_tokens,
  const std::string& active_generated_text
) const {
  if (!enabled_) {
    return;
  }

  std::ostringstream details;
  details << "raw_finish=" << ToString(raw_finish_reason) << " reported_finish=" << ToString(reported_finish_reason)
          << " appended_stop_tokens=" << appended_stop_tokens;
  if (content_ && !active_generated_text.empty()) {
    details << " gen_tail_preview=\"" << EscapePreview(active_generated_text, 160) << '"';
  }
  LogTimingEvent("generation_stop_tail", details.str());
}

} // namespace giga_edge
