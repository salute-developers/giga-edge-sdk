#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <string>

namespace giga_edge {

class CoalesceTextNode : public NodeBase {
public:
  explicit CoalesceTextNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_OPTIONAL_INPUT(coalesce_text, first, First, std::string, "Text that wins when it is not empty");
  REGISTER_INPUT(coalesce_text, second, Second, std::string, "Text used when the first one is empty");
  REGISTER_OUTPUT(coalesce_text, text, Text, std::string, "The first non-empty text; empty when both are");
};

REGISTER_NODE(
  coalesce_text,
  CoalesceTextNode,
  "Picks the first non-empty text of two, e.g. merges typed text and a recognized utterance into one lane"
);

} // namespace giga_edge
