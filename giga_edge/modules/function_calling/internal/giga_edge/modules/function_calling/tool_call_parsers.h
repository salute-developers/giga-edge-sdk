#pragma once

#include "giga_edge/modules/function_calling/compact_tool_call_parser.h"

namespace giga_edge {

REGISTER_TOOL_CALL_PARSER(compact_tool_call_v1, [](const YAML::Node& params) {
  return std::make_shared<CompactToolCallParser>(ParseCompactToolCallParserParams(params));
})

} // namespace giga_edge
