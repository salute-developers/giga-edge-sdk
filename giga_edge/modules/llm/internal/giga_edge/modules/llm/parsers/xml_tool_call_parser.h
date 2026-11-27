#pragma once

#include "giga_edge/modules/assistant_core/tool_call_parser.h"

#include <string>

namespace giga_edge {

struct XmlToolCallParserParams {
  std::string call_open = "<tool_call>";
  std::string call_close = "</tool_call>";
};

XmlToolCallParserParams ParseXmlToolCallParserParams(const YAML::Node& config);

class XmlToolCallParser : public IToolCallParser {
public:
  explicit XmlToolCallParser(XmlToolCallParserParams params);

  std::optional<size_t> FindStart(std::string_view text) const override;
  std::optional<size_t> FindEnd(std::string_view call_text, bool is_final) const override;
  size_t PartialStartLength(std::string_view text) const override;
  std::vector<ToolCall> Parse(std::string_view call_text) const override;
  std::string CleanText(std::string_view text) const override;

private:
  XmlToolCallParserParams params_;
};

} // namespace giga_edge
