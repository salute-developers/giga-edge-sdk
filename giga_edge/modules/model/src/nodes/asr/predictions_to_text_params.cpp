#include "giga_edge/modules/model/nodes/asr/predictions_to_text_params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>

namespace giga_edge {
namespace {

TokenizerType ParseTokenizerType(const YAML::Node& config) {
  const auto value = config["tokenizer_type"].as<std::string>("plain");
  if (value == "plain") {
    return TokenizerType::PLAIN;
  }
  if (value == "sentencepiece" || value == "bpe") {
    return TokenizerType::SENTENCEPIECE;
  }
  throw std::runtime_error("Unknown predictions_to_text tokenizer_type: " + value);
}

} // namespace

template<>
PredictionsToTextNodeParams ParseConfig(const YAML::Node& config) {
  PredictionsToTextNodeParams result;

  result.dict_path = config["dict_path"].as<std::string>();
  result.eos_token = config["eos_token"].as<std::string>("");
  result.blank_token = config["blank_token"].as<std::string>("");
  result.ignore_duplicates = config["ignore_duplicates"].as<bool>(false);
  result.tokenizer_type = ParseTokenizerType(config);
  result.capitalize_sentences = config["capitalize_sentences"].as<bool>(false);
  result.proper_nouns_path = config["proper_nouns_path"].as<std::string>("");
  return result;
}

} // namespace giga_edge