#include "giga_edge/modules/llm/nodes/llm_passthrough/node.h"

#include <utility>

namespace giga_edge {

LlmPassthrough::LlmPassthrough(const NodeBaseConfig& base_config)
  : NodeBase(base_config) {}

void LlmPassthrough::ProcessOnce() {
  const auto& request = GetAssistantRequest();
  const auto push = [this, &request](AssistantChunk chunk) {
    chunk.meta.request_id = request.request_id;
    if (HasLlmReportOutput()) {
      PushLlmReport(
        LlmReport{
          .request_id = request.request_id,
          .phase = chunk.meta.phase,
          .is_final = chunk.meta.is_final,
          .finish_reason = chunk.meta.finish_reason,
          .status = chunk.meta.status,
          .error = chunk.meta.error,
        }
      );
    }
    PushStreamChunk(std::move(chunk));
  };

  if (request.status != AssistantStatus::OK) {
    push(MakeErrorChunk(request.error.empty() ? "upstream request error" : request.error));
    return;
  }
  if (request.kind != AssistantRequestKind::GENERATE) {
    push(MakeIdleChunk());
    return;
  }
  const std::string& text = GetText();
  push(MakeTextChunk(text, text));
  push(MakeFinalChunk(StreamFinishReason::COMPLETED));
}

} // namespace giga_edge
