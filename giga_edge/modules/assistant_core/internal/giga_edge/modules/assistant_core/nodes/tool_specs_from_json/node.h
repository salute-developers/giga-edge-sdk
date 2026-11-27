#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <string>

namespace giga_edge {

class ToolSpecsFromJson : public NodeBase {
public:
  explicit ToolSpecsFromJson(const NodeBaseConfig& base_config);

  void ProcessOnce() override;
  void Reset() override {}

private:
  REGISTER_INPUT(
    tool_specs_from_json,
    json,
    Json,
    std::string,
    "JSON array of tools: [{\"name\", \"description\", \"parameters\"}]; empty string means no tools."
  );
  REGISTER_OUTPUT(tool_specs_from_json, tool_specs, ToolSpecs, ToolSpecs, "Tool catalog supplied by the host.");
};

REGISTER_NODE(tool_specs_from_json, ToolSpecsFromJson, "Parses a host-provided JSON tool catalog into ToolSpecs.");

} // namespace giga_edge
