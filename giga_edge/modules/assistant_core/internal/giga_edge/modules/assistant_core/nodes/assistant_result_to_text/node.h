#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <string>

namespace giga_edge {

struct AssistantResultToTextParams {
  std::string fallback_text;
};

template<>
AssistantResultToTextParams ParseConfig(const YAML::Node& config);

class AssistantResultToText : public NodeBase {
public:
  AssistantResultToText(const NodeBaseConfig& base_config, const AssistantResultToTextParams& params);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(
    assistant_result_to_text, assistant_result, AssistantResult, AssistantResult, "Finished assistant result."
  );
  REGISTER_OUTPUT(
    assistant_result_to_text,
    text,
    Text,
    std::string,
    "Reply text without tool calls, fallback_text on a failed turn; empty on every other tick."
  );

  AssistantResultToTextParams params_;
};

REGISTER_NODE(
  assistant_result_to_text,
  AssistantResultToText,
  AssistantResultToTextParams,
  "Takes the reply text out of an assistant result, e.g. to feed synthesis."
);

} // namespace giga_edge
