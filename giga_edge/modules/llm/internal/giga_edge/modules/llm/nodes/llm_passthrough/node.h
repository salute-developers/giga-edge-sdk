#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"
#include "giga_edge/modules/llm/llm_report.h"

#include <string>

namespace giga_edge {

class LlmPassthrough : public NodeBase {
public:
  explicit LlmPassthrough(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(
    llm_passthrough,
    assistant_request,
    AssistantRequest,
    AssistantRequest,
    "Request from the dialogue builder; kind=GENERATE starts an answer, anything else is an idle tick."
  );
  REGISTER_INPUT(
    llm_passthrough, text, Text, std::string, "The user text that the request was built from; it becomes the answer."
  );
  REGISTER_BATCH_OUTPUT(
    llm_passthrough, stream_chunk, StreamChunk, AssistantChunk, "The text as one delta followed by a final chunk."
  );
  REGISTER_OPTIONAL_BATCH_OUTPUT(llm_passthrough, llm_report, LlmReport, LlmReport, "Report with zero token counts.");
};

REGISTER_NODE(
  llm_passthrough,
  LlmPassthrough,
  "Runner without a model: returns the user text as the answer. Selected by a feature instead of llm_generate "
  "when post-processing is off, so the model is unloaded while the graph keeps its shape."
);

} // namespace giga_edge
