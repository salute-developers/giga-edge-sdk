#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/prosody.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace giga_edge::vits {
namespace {

// Byte offsets of the letters of a range.
std::vector<size_t> Letters(const std::string& text, const TextRange& range) {
  std::vector<size_t> letters;
  for (auto pos = range.begin; pos < range.begin + range.size;) {
    const auto symbol = DecodeUtf8(text, pos);
    if (IsLetter(symbol.value)) {
      letters.push_back(pos);
    }
    pos += symbol.size;
  }
  return letters;
}

void CheckShape(const std::vector<int>& shape, size_t tokens, const VoiceConfig& voice) {
  const auto& dims = voice.embedding_dims;
  if (
    !dims.empty() && shape.size() == 4 && shape[0] == 1 && shape[1] == static_cast<int>(tokens)
    && shape[2] >= *std::max_element(dims.begin(), dims.end()) && shape[3] == static_cast<int>(dims.size())
  ) {
    return;
  }
  std::string text;
  for (const auto dim : shape) {
    text += (text.empty() ? "" : ", ") + std::to_string(dim);
  }
  throw std::invalid_argument(
    "vits: qf0_logits of shape [" + text + "], expected [1, " + std::to_string(tokens) + ", classes >= embedding_dims, "
    + std::to_string(dims.size()) + "]"
  );
}

} // namespace

std::vector<int64_t> MakeProsodyLabels(
  const PreparedSentence& sentence,
  const std::vector<WordPieceToken>& tokens,
  const float* qf0_logits,
  const std::vector<int>& qf0_shape,
  const VoiceConfig& voice,
  const QuestionRules* question_rules
) {
  const auto& dims = voice.embedding_dims;
  const auto features = dims.size();
  const auto& plain = sentence.plain;
  const auto& marked = sentence.marked;
  CheckShape(qf0_shape, tokens.size(), voice);
  CheckWords(sentence);
  const auto classes = static_cast<size_t>(qf0_shape[2]);

  // Per byte of plain: the token of a letter there, -1 when there is none.
  std::vector<ptrdiff_t> token_at(plain.size(), -1);
  for (size_t token = 0; token < tokens.size(); ++token) {
    const auto& range = tokens[token].range;
    if (range.begin > plain.size() || range.size > plain.size() - range.begin) {
      throw std::invalid_argument("vits: token " + std::to_string(token) + " is out of the plain text");
    }
    if (!tokens[token].unknown && range.size > 0 && IsLetter(DecodeUtf8(plain, range.begin).value)) {
      std::fill_n(token_at.begin() + static_cast<ptrdiff_t>(range.begin), range.size, static_cast<ptrdiff_t>(token));
    }
  }
  std::vector<bool> question(plain.size(), false);
  if (question_rules != nullptr && voice.use_question_rules) {
    for (const auto& span : question_rules->Find(sentence)) {
      std::fill_n(question.begin() + static_cast<ptrdiff_t>(span.begin), span.size, true);
    }
  }

  std::vector<size_t> symbols; // byte offsets in marked
  size_t letters = 0;
  for (size_t pos = 0; pos < marked.size();) {
    const auto symbol = DecodeUtf8(marked, pos);
    symbols.push_back(pos);
    letters += IsLetter(symbol.value) ? 1 : 0;
    pos += symbol.size;
  }
  std::vector<int64_t> rows;
  rows.reserve(symbols.size() * features);
  for (size_t i = 0; i < symbols.size(); ++i) {
    rows.insert(rows.end(), dims.begin(), dims.end());
  }

  size_t symbol = 0;
  size_t word_letters = 0;
  for (const auto& word : sentence.words) {
    const auto from = Letters(plain, word.plain);
    const auto to = Letters(marked, word.marked);
    word_letters += to.size();
    for (size_t k = 0; k < to.size() && !from.empty(); ++k) {
      const auto source = from[k * from.size() / to.size()];
      const auto token = token_at[source];
      if (token < 0) {
        continue;
      }
      while (symbols[symbol] != to[k]) {
        ++symbol;
      }
      auto* row = rows.data() + symbol * features;
      const float* logits = qf0_logits + static_cast<size_t>(token) * classes * features;
      for (size_t feature = 0; feature < features; ++feature) {
        size_t best = 0;
        for (size_t label = 1; label < static_cast<size_t>(dims[feature]); ++label) {
          if (logits[label * features + feature] > logits[best * features + feature]) {
            best = label;
          }
        }
        row[feature] = static_cast<int64_t>(best);
      }
      for (const auto& [feature, label] : voice.forced_labels) {
        row[feature] = label;
      }
      if (question[source]) {
        for (const auto& [feature, label] : voice.question_labels) {
          row[feature] = label;
        }
      }
    }
  }
  if (word_letters != letters) {
    throw std::invalid_argument("vits: a letter of the marked text is outside the words");
  }

  if (!voice.blank_intersperse) {
    return rows;
  }
  std::vector<int64_t> result(dims.begin(), dims.end());
  result.reserve((2 * symbols.size() + 1) * features);
  for (size_t i = 0; i < symbols.size(); ++i) {
    const auto row = rows.begin() + static_cast<ptrdiff_t>(i * features);
    result.insert(result.end(), row, row + static_cast<ptrdiff_t>(features));
    result.insert(result.end(), row, row + static_cast<ptrdiff_t>(features));
  }
  return result;
}

} // namespace giga_edge::vits
