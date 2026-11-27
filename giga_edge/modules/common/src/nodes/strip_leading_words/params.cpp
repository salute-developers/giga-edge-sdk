#include "giga_edge/modules/common/nodes/strip_leading_words/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>

namespace giga_edge {

template<>
StripLeadingWordsNodeParams ParseConfig(const YAML::Node& config) {
  StripLeadingWordsNodeParams result;
  result.words = config["words"].as<std::vector<std::string>>(result.words);
  const auto delimiter = config["words_delimiter"].as<std::string>(std::string(1, result.words_delimiter));
  if (delimiter.size() != 1) {
    throw std::invalid_argument("strip_leading_words: words_delimiter must be a single character");
  }
  result.words_delimiter = delimiter.front();
  result.repeat = config["repeat"].as<bool>(result.repeat);
  result.typos_per_letters = config["typos_per_letters"].as<int>(result.typos_per_letters);
  if (result.typos_per_letters < 0) {
    throw std::invalid_argument("strip_leading_words: typos_per_letters must not be negative");
  }
  result.max_skipped_tokens = config["max_skipped_tokens"].as<int>(result.max_skipped_tokens);
  if (result.max_skipped_tokens < 0) {
    throw std::invalid_argument("strip_leading_words: max_skipped_tokens must not be negative");
  }
  result.split_glued = config["split_glued"].as<bool>(result.split_glued);
  result.capitalize_rest = config["capitalize_rest"].as<bool>(result.capitalize_rest);
  result.drop_unmatched = config["drop_unmatched"].as<bool>(result.drop_unmatched);
  result.hold_words = config["hold_words"].as<bool>(result.hold_words);
  return result;
}

} // namespace giga_edge
