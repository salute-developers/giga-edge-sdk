#pragma once

#include "params.h"

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"

#include <string>
#include <vector>

namespace giga_edge {

class StripLeadingWordsNode : public NodeBase {
public:
  using Word = std::vector<std::u16string>;

  StripLeadingWordsNode(const NodeBaseConfig& base_config, const StripLeadingWordsNodeParams& params);

  void ProcessOnce() override;

  void Reset() override {}

  REGISTER_INPUT(strip_leading_words, input, Input, std::string, "Text to strip");
  REGISTER_OPTIONAL_INPUT(
    strip_leading_words,
    words,
    Words,
    std::string,
    "Words to strip in addition to the configured ones, joined by `words_delimiter`; with `hold_words` an empty "
    "value keeps the last ones"
  );
  REGISTER_OUTPUT(
    strip_leading_words,
    output,
    Output,
    std::string,
    "Text without the leading words; empty with `drop_unmatched` when there were words but none led the text"
  );

private:
  const std::vector<Word>& InputWords();

  StripLeadingWordsNodeParams params_;
  std::vector<Word> configured_words_;
  std::string input_words_source_;
  std::vector<Word> input_words_;
};

REGISTER_NODE(
  strip_leading_words,
  StripLeadingWordsNode,
  StripLeadingWordsNodeParams,
  "Strips the leading words of a text if they match a configured or supplied set"
);

} // namespace giga_edge
