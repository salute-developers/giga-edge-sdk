#pragma once

#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/model/nodes/asr/predictions_to_text_params.h"

#include <string_view>

namespace giga_edge {

class PredictionsToTextNode : public NodeBase {
public:
  PredictionsToTextNode(
    const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const PredictionsToTextNodeParams& params
  );

  void ProcessOnce() override;

  void Reset() override;

  REGISTER_INPUT(predictions_to_text, predictions, Predictions, std::vector<float>, "Token predicitons");
  REGISTER_OPTIONAL_INPUT(predictions_to_text, push, Push, bool, "End of utterance: release the held-back last word");
  REGISTER_OUTPUT(predictions_to_text, text, Text, std::string, "Recognized text");

private:
  void AppendToken(const std::string& token);
  void ReleasePending(bool all);
  void Capitalize(std::string& text);
  bool IsProperNoun(std::string_view word) const;

  const PredictionsToTextNodeParams params_;
  std::vector<std::string> token_dictionary_;
  std::vector<size_t> proper_nouns_; // sorted hashes of the word forms
  std::string last_token_;
  std::string current_text_;
  bool sentencepiece_at_start_ = true;
  bool capitalize_next_ = true;
  bool in_word_ = false;
  std::string pending_;
};

REGISTER_NODE(
  predictions_to_text, PredictionsToTextNode, PredictionsToTextNodeParams, true, "Gathers toked predictions into text"
);

} // namespace giga_edge
