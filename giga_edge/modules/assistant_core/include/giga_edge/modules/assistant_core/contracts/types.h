#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace giga_edge {

enum class AssistantStatus {
  OK,
  ERROR,
  SKIPPED,
};

enum class RunnerPhase {
  IDLE,
  PREFILLING,
  GENERATING,
};

enum class StreamFinishReason {
  NONE,
  COMPLETED,
  EOS,
  STOP_SEQUENCE,
  MAX_TOKENS,
  CANCELLED,
  ERROR,
  // Declined before generation: the fallback path replies, not added to the dialogue history.
  DECLINED,
};

enum class MessageRole {
  SYSTEM,
  SCENE,
  USER,
  ASSISTANT,
  TOOL,
};

enum class AssistantRequestKind {
  NONE,
  WARM,
  GENERATE,
};

struct UserQuery {
  std::string request_id;
  std::string text;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct SceneSnapshot {
  std::string text;
  std::string version;
  bool allow_empty = true;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct ToolSpec {
  std::string name;
  std::string description;
  std::string parameters_json;
};

struct ToolSpecs {
  std::vector<ToolSpec> tools;
  std::string revision;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct ToolCall {
  std::string id;
  std::string name;
  std::string arguments_json;
  std::string raw_text;
};

struct ToolResult {
  std::string call_id;
  std::string name;
  std::string content;
  bool is_ready = false;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct AssistantMessage {
  MessageRole role = MessageRole::USER;
  std::string content;
  std::vector<ToolCall> tool_calls;
  std::string tool_call_id;
};

struct GenerationParams {
  std::optional<float> temperature;
  int max_tokens = 0;
  std::vector<std::string> stop;
};

struct AssistantRequest {
  AssistantRequestKind kind = AssistantRequestKind::NONE;
  std::string request_id;
  std::vector<AssistantMessage> messages;
  std::vector<ToolSpec> tools;
  GenerationParams generation;
  // Reply to the assistant's clarifying question: the prompt head does not decline it.
  bool follow_up = false;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
};

struct GenerationUsage {
  // At the end of the request: an interpreter failure resets the context.
  size_t context_tokens = 0;
  size_t reused_tokens = 0;
  size_t prefilled_tokens = 0;
  size_t generated_tokens = 0;
  // Since the request start.
  double prefill_ms = 0;
  double generation_ms = 0;
  // Empty if the prompt head did not decide.
  std::string prompt_label;
};

struct ChunkMeta {
  std::string request_id;
  bool is_final = false;
  StreamFinishReason finish_reason = StreamFinishReason::NONE;
  RunnerPhase phase = RunnerPhase::IDLE;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
  GenerationUsage usage;
};

struct TextDelta {
  std::string text;
  std::string raw;
  // Model reply for the dialogue history (raw with special texts escaped); arrives once at the end of the turn.
  std::string history;
};

struct AssistantChunk {
  ChunkMeta meta;
  std::variant<std::monostate, TextDelta, ToolCall> payload;
};

struct TurnStep {
  double ms = 0;
  GenerationUsage usage;
  std::string raw_text;
  bool has_call = false;
  ToolCall call;
  bool has_result = false;
  std::string result;
  double result_ms = 0;
};

// Empty request_id: no turn.
struct TurnTrace {
  std::string request_id;
  std::string user;
  double wait_ms = 0;
  double ms = 0;
  std::vector<TurnStep> steps;
  bool finished = false;
  AssistantStatus status = AssistantStatus::OK;
  StreamFinishReason finish_reason = StreamFinishReason::NONE;
  std::string error;
};

struct AssistantResult {
  std::string request_id;
  std::string raw_text;
  std::string text;
  std::vector<ToolCall> tool_calls;
  bool is_final = false;
  StreamFinishReason finish_reason = StreamFinishReason::NONE;
  RunnerPhase phase = RunnerPhase::IDLE;
  AssistantStatus status = AssistantStatus::OK;
  std::string error;
  GenerationUsage usage;
  TurnTrace trace;
  // Calls of the same batch after this one: already generated, no new generation before them.
  size_t queued_calls = 0;
  // Text for the history and the prompt; empty if the source has no provenance, the history takes raw_text.
  std::string history_text;
};

struct ToolStep {
  std::string assistant_raw;
  ToolCall call;
  ToolResult result;
  bool has_result = false;
  size_t queued_calls = 0;
};

struct DialogueTurn {
  std::string request_id;
  std::string user;
  std::string scene_text;
  std::string scene_version;
  std::vector<ToolStep> steps;
  std::string assistant;
  bool completed = false;
};

struct DialogueHistory {
  std::vector<DialogueTurn> turns;
  DialogueTurn pending;
  bool has_pending = false;
  DialogueTurn queued;
  bool has_queued = false;
  std::string last_scene_version;
};

inline const TextDelta* ChunkText(const AssistantChunk& chunk) {
  return std::get_if<TextDelta>(&chunk.payload);
}

inline const ToolCall* ChunkToolCall(const AssistantChunk& chunk) {
  return std::get_if<ToolCall>(&chunk.payload);
}

inline AssistantChunk MakeIdleChunk(RunnerPhase phase = RunnerPhase::IDLE) {
  AssistantChunk chunk;
  chunk.meta.phase = phase;
  return chunk;
}

inline AssistantChunk MakeTextChunk(std::string text, std::string raw, RunnerPhase phase = RunnerPhase::GENERATING) {
  AssistantChunk chunk;
  chunk.meta.phase = phase;
  chunk.payload = TextDelta{std::move(text), std::move(raw)};
  return chunk;
}

inline AssistantChunk MakeToolCallChunk(ToolCall call, RunnerPhase phase = RunnerPhase::GENERATING) {
  AssistantChunk chunk;
  chunk.meta.phase = phase;
  chunk.payload = std::move(call);
  return chunk;
}

inline AssistantChunk MakeFinalChunk(StreamFinishReason reason) {
  AssistantChunk chunk;
  chunk.meta.is_final = true;
  chunk.meta.finish_reason = reason;
  return chunk;
}

inline AssistantChunk MakeErrorChunk(std::string error, StreamFinishReason reason = StreamFinishReason::ERROR) {
  AssistantChunk chunk;
  chunk.meta.is_final = true;
  chunk.meta.finish_reason = reason;
  chunk.meta.status = AssistantStatus::ERROR;
  chunk.meta.error = std::move(error);
  return chunk;
}

inline bool IsStreamFinished(const AssistantChunk& chunk) {
  return chunk.meta.is_final || chunk.meta.finish_reason != StreamFinishReason::NONE;
}

inline bool HasSceneText(const SceneSnapshot& scene) {
  return !scene.text.empty();
}

inline bool IsReadyToolResult(const ToolResult& result) {
  return result.is_ready && result.status == AssistantStatus::OK;
}

inline bool HasToolCalls(const AssistantResult& result) {
  return !result.tool_calls.empty();
}

inline bool IsCommitted(const AssistantResult& result) {
  return result.is_final && result.status == AssistantStatus::OK
    && result.finish_reason != StreamFinishReason::CANCELLED && result.finish_reason != StreamFinishReason::ERROR
    && result.finish_reason != StreamFinishReason::DECLINED;
}

inline std::string ToString(AssistantStatus status) {
  switch (status) {
    case AssistantStatus::OK:
      return "ok";
    case AssistantStatus::ERROR:
      return "error";
    case AssistantStatus::SKIPPED:
      return "skipped";
  }
  return "unknown";
}

inline std::string ToString(RunnerPhase phase) {
  switch (phase) {
    case RunnerPhase::IDLE:
      return "idle";
    case RunnerPhase::PREFILLING:
      return "prefilling";
    case RunnerPhase::GENERATING:
      return "generating";
  }
  return "unknown";
}

inline std::string ToString(StreamFinishReason finish_reason) {
  switch (finish_reason) {
    case StreamFinishReason::NONE:
      return "none";
    case StreamFinishReason::COMPLETED:
      return "completed";
    case StreamFinishReason::EOS:
      return "eos";
    case StreamFinishReason::STOP_SEQUENCE:
      return "stop_sequence";
    case StreamFinishReason::MAX_TOKENS:
      return "max_tokens";
    case StreamFinishReason::CANCELLED:
      return "cancelled";
    case StreamFinishReason::ERROR:
      return "error";
    case StreamFinishReason::DECLINED:
      return "declined";
  }
  return "unknown";
}

inline std::string ToString(MessageRole role) {
  switch (role) {
    case MessageRole::SYSTEM:
      return "system";
    case MessageRole::SCENE:
      return "scene";
    case MessageRole::USER:
      return "user";
    case MessageRole::ASSISTANT:
      return "assistant";
    case MessageRole::TOOL:
      return "tool";
  }
  return "unknown";
}

inline std::string ToString(AssistantRequestKind kind) {
  switch (kind) {
    case AssistantRequestKind::NONE:
      return "none";
    case AssistantRequestKind::WARM:
      return "warm";
    case AssistantRequestKind::GENERATE:
      return "generate";
  }
  return "unknown";
}

} // namespace giga_edge
