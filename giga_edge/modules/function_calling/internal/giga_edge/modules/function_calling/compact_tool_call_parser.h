#pragma once

#include "giga_edge/modules/assistant_core/tool_call_parser.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge {

// A tag argument: <prefix name> and, if indexed, a number after it (<d_light>0 -> light_0).
struct CompactArgFormat {
  std::vector<std::string> tag_prefixes;
  bool indexed = false;
  bool as_string = false;
};

struct CompactArgSpec {
  std::string name;
  CompactArgFormat format;
  // The call may end before it: then it and the following ones are absent from the arguments.
  bool optional = false;
};

struct CompactFunctionSpec {
  std::string name;
  std::string token;
  std::vector<CompactArgSpec> args;
};

struct CompactToolCallParserParams {
  std::string start_function_call_token;
  std::vector<std::string> function_token_prefixes;
  std::string arg_separator;
  std::vector<std::string> stop_markers;
  // Tags that stay in the answer text (model masks substituted further down the graph); the rest are cut out.
  std::vector<std::string> keep_tags;
  std::vector<CompactFunctionSpec> function_specs;
  // Several calls in one generation; the raw_text of a call is canonical.
  bool parallel_tool_calls = false;
};

CompactToolCallParserParams ParseCompactToolCallParserParams(const YAML::Node& config);

class CompactToolCallParser : public IToolCallParser {
public:
  explicit CompactToolCallParser(CompactToolCallParserParams params);

  std::optional<size_t> FindStart(std::string_view text) const override;
  std::optional<size_t> FindEnd(std::string_view call_text, bool is_final) const override;
  size_t PartialStartLength(std::string_view text) const override;
  std::vector<ToolCall> Parse(std::string_view call_text) const override;
  std::string CleanText(std::string_view text) const override;
  std::vector<std::vector<RawTextSource>> RawTextSources(
    std::string_view call_text, const std::vector<ToolCall>& calls
  ) const override;

private:
  // The slot value and where it comes from in call_text; a tag has no source: the config writes it.
  struct Slot {
    std::string text;
    std::string_view source;
  };

  std::vector<ToolCall> ParseAll(std::string_view call_text, std::vector<std::vector<RawTextSource>>* sources) const;
  bool IsFunctionTokenAt(std::string_view text, size_t at) const;
  std::optional<ToolCall> ParseCall(
    std::string_view body, std::vector<std::pair<size_t, std::string_view>>* sources
  ) const;
  std::optional<std::string> ParseArguments(
    const CompactFunctionSpec& spec, std::string_view text, std::vector<Slot>& slots
  ) const;

  CompactToolCallParserParams params_;
  std::unordered_map<std::string, size_t> by_token_;
};

} // namespace giga_edge
