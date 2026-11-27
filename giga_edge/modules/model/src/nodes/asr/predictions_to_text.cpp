#include "giga_edge/modules/model/nodes/asr/predictions_to_text.h"

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/token_dictionary/token_dictionary.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <string_view>

namespace giga_edge {
namespace {

std::vector<size_t> ProperNounsFromFile(const IDataReaderPtr& data_reader, const std::string& path) {
  if (path.empty()) {
    return {};
  }
  auto hashes = TokenDictionaryFromFile<size_t>(data_reader, path, [](std::string_view word) {
    return std::hash<std::string_view>{}(word);
  });
  std::sort(hashes.begin(), hashes.end());
  return hashes;
}

bool IsWordByte(unsigned char c) {
  return std::isalnum(c) || c == '\'' || c >= 0x80;
}

// An explicit mapping for Latin and Cyrillic: towupper leaves Cyrillic untouched under the C locale.
void UpperFirst(std::string& text, size_t i) {
  const auto c = static_cast<unsigned char>(text[i]);
  if (c >= 'a' && c <= 'z') {
    text[i] = static_cast<char>(c - 'a' + 'A');
    return;
  }
  if (i + 1 >= text.size()) {
    return;
  }
  auto& next = reinterpret_cast<unsigned char&>(text[i + 1]);
  if (c == 0xD0 && next >= 0xB0 && next <= 0xBF) { // Cyrillic a..pe (U+0430..U+043F)
    next -= 0x20;
  } else if (c == 0xD1 && next >= 0x80 && next <= 0x8F) { // Cyrillic er..ya (U+0440..U+044F)
    text[i] = static_cast<char>(0xD0);
    next += 0x20;
  } else if (c == 0xD1 && next == 0x91) { // Cyrillic yo (U+0451)
    text[i] = static_cast<char>(0xD0);
    next = 0x81;
  }
}

constexpr std::string_view kSentencePieceSpace = "\xE2\x96\x81";
constexpr std::array kSentencePieceControlTokens = {
  std::string_view{"<s>"},
  std::string_view{"</s>"},
  std::string_view{"<pad>"},
};

} // namespace

PredictionsToTextNode::PredictionsToTextNode(
  const NodeBaseConfig& base_config, const IDataReaderPtr& data_reader, const PredictionsToTextNodeParams& params
)
  : NodeBase(base_config)
  , params_(params)
  , token_dictionary_(
      TokenDictionaryFromFile<std::string>(
        data_reader,
        params.dict_path,
        [](std::string_view s) {
          return std::string{s};
        }
      )
    )
  , proper_nouns_(ProperNounsFromFile(data_reader, params.proper_nouns_path)) {}

void PredictionsToTextNode::ProcessOnce() {
  const auto& predictions = GetPredictions();

  bool need_push = params_.eos_token.empty();

  if (predictions.size() % token_dictionary_.size() != 0) {
    throw std::runtime_error(
      std::string("PredictionsToTextNode: Got wrong predictions size. Expected multiple of ")
      + std::to_string(token_dictionary_.size()) + ", found " + std::to_string(predictions.size())
    );
  }

  for (auto it = predictions.begin(); it != predictions.end(); it += token_dictionary_.size()) {
    auto token_id = std::max_element(it, it + token_dictionary_.size()) - it;
    auto token = token_dictionary_.at(token_id);

    if (!params_.eos_token.empty() && token == params_.eos_token) {
      need_push = true;
      break;
    }

    if (params_.ignore_duplicates && token == last_token_) {
      continue;
    }

    last_token_ = token;
    if (token != params_.blank_token) {
      AppendToken(token);
    }
  }

  // push is the end of the utterance: release everything and start the next one from scratch.
  const auto push = GetPush();
  const bool end_of_utterance = push.has_value() && push.value().get();
  ReleasePending(end_of_utterance);
  PushText(need_push || end_of_utterance ? std::move(current_text_) : std::string{});
  if (end_of_utterance) {
    Reset();
  }
}

void PredictionsToTextNode::Reset() {
  current_text_ = "";
  last_token_ = params_.blank_token;
  sentencepiece_at_start_ = true;
  capitalize_next_ = true;
  in_word_ = false;
  pending_.clear();
}

void PredictionsToTextNode::AppendToken(const std::string& token) {
  std::string decoded;
  if (params_.tokenizer_type == TokenizerType::PLAIN) {
    decoded = token;
  } else {
    if (
      std::find(kSentencePieceControlTokens.begin(), kSentencePieceControlTokens.end(), token)
      != kSentencePieceControlTokens.end()
    ) {
      return;
    }
    decoded.reserve(token.size());
    std::string_view remaining = token;
    while (true) {
      const auto separator = remaining.find(kSentencePieceSpace);
      if (separator == std::string_view::npos) {
        decoded.append(remaining);
        break;
      }
      decoded.append(remaining.substr(0, separator));
      decoded.push_back(' ');
      remaining.remove_prefix(separator + kSentencePieceSpace.size());
    }
    if (sentencepiece_at_start_) {
      const auto first_character = decoded.find_first_not_of(' ');
      if (first_character == std::string::npos) {
        return;
      }
      decoded.erase(0, first_character);
      sentencepiece_at_start_ = false;
    }
  }
  pending_ += decoded;
}

void PredictionsToTextNode::ReleasePending(bool all) {
  auto end = pending_.size();
  if (!all && !proper_nouns_.empty()) {
    while (end > 0 && IsWordByte(pending_[end - 1])) {
      --end;
    }
  }
  auto ready = pending_.substr(0, end);
  pending_.erase(0, end);
  if (params_.capitalize_sentences || !proper_nouns_.empty()) {
    Capitalize(ready);
  }
  current_text_ += ready;
}

void PredictionsToTextNode::Capitalize(std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    if (!IsWordByte(c)) {
      capitalize_next_ = capitalize_next_ || c == '.' || c == '?' || c == '!';
      in_word_ = false;
      ++i;
      continue;
    }
    auto end = i;
    while (end < text.size() && IsWordByte(text[end])) {
      ++end;
    }
    if (!in_word_) {
      if ((params_.capitalize_sentences && capitalize_next_) || IsProperNoun({text.data() + i, end - i})) {
        UpperFirst(text, i);
      }
      capitalize_next_ = false;
    }
    in_word_ = true;
    i = end;
  }
}

bool PredictionsToTextNode::IsProperNoun(std::string_view word) const {
  if (word == "i" || word.substr(0, 2) == "i'") { // English I, I'm
    return true;
  }
  return std::binary_search(proper_nouns_.begin(), proper_nouns_.end(), std::hash<std::string_view>{}(word));
}

} // namespace giga_edge
