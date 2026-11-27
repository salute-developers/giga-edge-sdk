#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <string>
#include <vector>

namespace giga_edge {

struct CompactToolResult {
  std::string call_id;
  std::string name;
  std::string value_json;
  bool is_error = false;
  std::string error;
  // Success in the function's own format: passed to the model as is, without templates.
  std::string content;
};

using CompactToolResults = std::vector<CompactToolResult>;

// Result texts as the model saw them in training: {value} is the JSON of the value, {error} is the error string.
struct CompactResultFormat {
  std::string success;
  std::string success_with_value;
  std::string failure;
  int value_decimals = 2;
};

CompactResultFormat ParseCompactResultFormat(const YAML::Node& config);
ToolResult FormatCompactToolResult(const CompactToolResult& result, const CompactResultFormat& format);

} // namespace giga_edge
