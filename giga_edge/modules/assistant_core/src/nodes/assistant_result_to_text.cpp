#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/nodes/assistant_result_to_text/node.h"

namespace giga_edge {

template<>
AssistantResultToTextParams ParseConfig(const YAML::Node& config) {
  AssistantResultToTextParams result;
  result.fallback_text = config["fallback_text"].as<std::string>(result.fallback_text);
  return result;
}

AssistantResultToText::AssistantResultToText(
  const NodeBaseConfig& base_config, const AssistantResultToTextParams& params
)
  : NodeBase(base_config)
  , params_(params) {}

void AssistantResultToText::ProcessOnce() {
  const auto& result = GetAssistantResult();
  if (!result.is_final || result.finish_reason == StreamFinishReason::CANCELLED) {
    PushText("");
    return;
  }
  if (result.status == AssistantStatus::ERROR) {
    PushText(params_.fallback_text);
    return;
  }
  PushText(result.status == AssistantStatus::OK && result.tool_calls.empty() ? result.text : std::string());
}

} // namespace giga_edge
