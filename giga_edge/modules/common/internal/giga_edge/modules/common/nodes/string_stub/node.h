#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class StringStubNode : public NodeBase {
public:
  explicit StringStubNode(const NodeBaseConfig& base_config);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(string_stub, input, Input, std::vector<float>, "Input");
  REGISTER_OUTPUT(string_stub, text, Text, std::string, "Empty string");
};

REGISTER_NODE(string_stub, StringStubNode, "Stub, that generates an empty string");

} // namespace giga_edge
