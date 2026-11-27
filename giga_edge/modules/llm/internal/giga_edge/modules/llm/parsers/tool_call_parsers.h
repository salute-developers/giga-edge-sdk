#pragma once

#include "giga_edge/modules/llm/parsers/xml_tool_call_parser.h"

namespace giga_edge {

REGISTER_TOOL_CALL_PARSER(xml_tool_call, [](const YAML::Node& params) {
  return std::make_shared<XmlToolCallParser>(ParseXmlToolCallParserParams(params));
})

} // namespace giga_edge
