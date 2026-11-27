#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <string>
#include <vector>

namespace giga_edge {

struct StreamToAssistantResultParams {
  bool emit_partial = false;
};

template<>
StreamToAssistantResultParams ParseConfig(const YAML::Node& config);

class StreamToAssistantResult : public NodeBase {
public:
  StreamToAssistantResult(const NodeBaseConfig& base_config, const StreamToAssistantResultParams& params);

  void ProcessOnce() override;
  void Reset() override;

private:
  REGISTER_BATCH_INPUT(
    stream_to_assistant_result,
    stream_chunk,
    StreamChunk,
    AssistantChunk,
    "Chunks from any runner: local, on-device or cloud."
  );
  REGISTER_OUTPUT(
    stream_to_assistant_result,
    assistant_result,
    AssistantResult,
    AssistantResult,
    "Accumulated answer; is_final marks the end of a generation."
  );
  REGISTER_OPTIONAL_OUTPUT(
    stream_to_assistant_result, text, Text, std::string, "Final clean answer text, empty until then."
  );

  // The request's final result, built from the accumulated data.
  AssistantResult Final(AssistantResult out) const;

  StreamToAssistantResultParams params_;
  std::string request_id_;
  std::string raw_;
  std::string history_;
  std::string text_;
  std::vector<ToolCall> calls_;
};

REGISTER_NODE(
  stream_to_assistant_result,
  StreamToAssistantResult,
  StreamToAssistantResultParams,
  "Accumulates a chunk stream into an AssistantResult with text and tool calls."
);

} // namespace giga_edge
