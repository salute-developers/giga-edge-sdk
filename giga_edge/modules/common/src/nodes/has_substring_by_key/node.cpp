#include "giga_edge/modules/common/nodes/has_substring_by_key/node.h"

#include "giga_edge/lib/token_dictionary/token_dictionary.h"

namespace giga_edge {

HasSubstringByKeyNode::HasSubstringByKeyNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const HasSubstringByKeyNodeParams& params
)
  : NodeBase(base_config) {
  for (const auto& [key, file] : params.substring_files) {
    substring_sets_[key] = TokenDictionaryFromFile<std::string>(data_reader, file, [](const std::string_view s) {
      return std::string{s};
    });
  }
}

void HasSubstringByKeyNode::ProcessOnce() {
  const auto& text = GetText();
  const auto& key = GetKey();

  auto substring_set_it = substring_sets_.find(key);
  if (substring_set_it == substring_sets_.end()) {
    PushHasSubstring(false);
    return;
  }

  for (const auto& substring : substring_set_it->second) {
    if (text.find(substring) != std::string::npos) {
      PushHasSubstring(true);
      return;
    }
  }

  PushHasSubstring(false);
}

} // namespace giga_edge
