#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/function_calling/compact_tool_result.h"

#include <deque>

namespace giga_edge {

template<>
CompactResultFormat ParseConfig(const YAML::Node& config);

class FcToolResultNode : public NodeBase {
public:
  FcToolResultNode(const NodeBaseConfig& config, const CompactResultFormat& format)
    : NodeBase(config)
    , format_(format) {}
  void ProcessOnce() override;
  void Reset() override {
    pending_.clear();
  }

private:
  REGISTER_INPUT(fc_tool_result, tool_results, ToolResults, CompactToolResults, "Normalized compact action results.");
  REGISTER_OUTPUT(
    fc_tool_result, tool_result, ToolResult, ToolResult, "Model-formatted result without role delimiters."
  );
  CompactResultFormat format_;
  std::deque<ToolResult> pending_;
};

REGISTER_NODE(
  fc_tool_result,
  FcToolResultNode,
  CompactResultFormat,
  "Formats normalized results for the compact function-calling model with texts from the config. "
  "value_decimals (default 2, non-negative) limits fractional digits in numeric values, including nested values."
);

} // namespace giga_edge
