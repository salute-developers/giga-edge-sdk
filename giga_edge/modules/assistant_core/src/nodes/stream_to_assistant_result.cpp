#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/nodes/stream_to_assistant_result/node.h"
#include "giga_edge/modules/assistant_core/text.h"

#include <optional>
#include <utility>

namespace giga_edge {
namespace {

// On a hop request_id is already a call id: the number continues (q/tool/2) instead of nesting (q/tool/1/tool/1).
std::string CallId(const std::string& request_id, size_t offset) {
  constexpr std::string_view tool_mark = "/tool/";
  const auto at = request_id.rfind(tool_mark);
  const auto number = at == std::string::npos ? std::string() : request_id.substr(at + tool_mark.size());
  if (!number.empty() && number.find_first_not_of("0123456789") == std::string::npos) {
    return request_id.substr(0, at + tool_mark.size()) + std::to_string(std::stoull(number) + offset);
  }
  return request_id + std::string(tool_mark) + std::to_string(offset);
}

} // namespace

template<>
StreamToAssistantResultParams ParseConfig(const YAML::Node& config) {
  StreamToAssistantResultParams result;
  result.emit_partial = config["emit_partial"].as<bool>(result.emit_partial);
  return result;
}

StreamToAssistantResult::StreamToAssistantResult(
  const NodeBaseConfig& base_config, const StreamToAssistantResultParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void StreamToAssistantResult::Reset() {
  request_id_.clear();
  raw_.clear();
  history_.clear();
  text_.clear();
  calls_.clear();
}

void StreamToAssistantResult::ProcessOnce() {
  AssistantResult out;
  bool done = false;
  std::optional<AssistantResult> final;

  for (const auto* chunk : GetStreamChunkBatch()) {
    if (!chunk->meta.request_id.empty()) {
      if (!request_id_.empty() && request_id_ != chunk->meta.request_id) {
        // The final result of the previous request is not lost because of chunks of the next one in the same batch.
        if (done) {
          final = Final(std::exchange(out, {}));
        }
        Reset();
        out = AssistantResult{};
        done = false;
      }
      request_id_ = chunk->meta.request_id;
    }
    out.request_id = request_id_;
    out.phase = chunk->meta.phase;
    if (const auto* delta = ChunkText(*chunk)) {
      raw_ += delta->raw;
      history_ += delta->history;
      text_ += delta->text;
    }
    if (const auto* call = ChunkToolCall(*chunk)) {
      calls_.push_back(*call);
    }
    if (chunk->meta.status == AssistantStatus::ERROR) {
      out.status = AssistantStatus::ERROR;
      out.error = chunk->meta.error.empty() ? "upstream generation error" : chunk->meta.error;
      out.finish_reason = chunk->meta.finish_reason == StreamFinishReason::NONE
        ? StreamFinishReason::ERROR
        : chunk->meta.finish_reason;
      out.usage = chunk->meta.usage;
      done = true;
    } else if (chunk->meta.is_final) {
      out.status = AssistantStatus::OK;
      out.finish_reason = chunk->meta.finish_reason;
      out.usage = chunk->meta.usage;
      done = true;
    }
  }

  if (done) {
    final = Final(std::exchange(out, {}));
    Reset();
  }
  if (final.has_value()) {
    if (HasTextOutput()) {
      PushText(
        final->status == AssistantStatus::OK && final->finish_reason != StreamFinishReason::CANCELLED ? final->text : ""
      );
    }
    PushAssistantResult(std::move(*final));
    return;
  }

  if (params_.emit_partial) {
    out.text = text_;
    out.raw_text = raw_;
  }
  if (HasTextOutput()) {
    PushText("");
  }
  PushAssistantResult(std::move(out));
}

AssistantResult StreamToAssistantResult::Final(AssistantResult out) const {
  out.is_final = true;
  out.raw_text = raw_;
  out.history_text = history_;
  out.text = Trim(text_);
  out.tool_calls = calls_;
  if (out.finish_reason == StreamFinishReason::MAX_TOKENS && !out.tool_calls.empty()) {
    out.status = AssistantStatus::ERROR;
    out.error = "Tool generation exceeded its token limit";
    out.tool_calls.clear();
  }
  for (size_t i = 0; i < out.tool_calls.size(); ++i) {
    if (out.tool_calls[i].id.empty() && !out.request_id.empty()) {
      out.tool_calls[i].id = CallId(out.request_id, i + 1);
    }
  }
  return out;
}

} // namespace giga_edge
