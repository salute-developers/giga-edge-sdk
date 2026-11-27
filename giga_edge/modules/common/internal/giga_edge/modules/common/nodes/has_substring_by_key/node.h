#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

namespace giga_edge {

class HasSubstringByKeyNode : public NodeBase {
public:
  HasSubstringByKeyNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const HasSubstringByKeyNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(has_substring_by_key, text, Text, std::string, "Text to search");
  REGISTER_INPUT(has_substring_by_key, key, Key, std::string, "Key to get substrings set");

  REGISTER_OUTPUT(
    has_substring_by_key, has_substring, HasSubstring, bool, "Whether text contains any substring from appropriate set"
  );

private:
  std::unordered_map<std::string, std::vector<std::string>> substring_sets_;
};

REGISTER_NODE(
  has_substring_by_key,
  HasSubstringByKeyNode,
  HasSubstringByKeyNodeParams,
  true,
  "Checks, whether text contains any substring from appropriate set"
);

} // namespace giga_edge
