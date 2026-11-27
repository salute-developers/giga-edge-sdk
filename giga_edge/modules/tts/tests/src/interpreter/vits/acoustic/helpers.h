#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/voice_config.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/prepared_sentence.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace giga_edge::vits::test {

inline std::vector<TextRange> LetterRuns(const std::string& text) {
  std::vector<TextRange> runs;
  bool in_run = false;
  for (size_t pos = 0; pos < text.size();) {
    const auto symbol = DecodeUtf8(text, pos);
    const bool letter = IsLetter(symbol.value);
    if (letter && !in_run) {
      runs.push_back({pos, 0});
    }
    if (letter) {
      runs.back().size = pos + symbol.size - runs.back().begin;
    }
    in_run = letter;
    pos += symbol.size;
  }
  return runs;
}

// The words are the letter runs of plain and of marked, paired in order.
inline PreparedSentence MakeSentence(const std::string& plain, const std::string& marked) {
  PreparedSentence sentence{plain, marked, {}};
  const auto from = LetterRuns(plain);
  const auto to = LetterRuns(marked);
  if (from.size() != to.size()) {
    throw std::logic_error("test: plain and marked have different numbers of words");
  }
  for (size_t i = 0; i < from.size(); ++i) {
    sentence.words.push_back({from[i], to[i]});
  }
  return sentence;
}

// Tokens of plain by their pieces in order ("##" continues a word), as BERT would split it; ids are the indices.
inline std::vector<WordPieceToken> MakeTokens(const std::string& plain, const std::vector<std::string>& pieces) {
  std::vector<WordPieceToken> tokens;
  size_t pos = 0;
  for (const auto& token : pieces) {
    const bool unknown = token == "[UNK]";
    const auto piece = token.rfind("##", 0) == 0 ? token.substr(2) : token;
    const auto begin = unknown ? plain.find_first_not_of(' ', pos) : plain.find(piece, pos);
    const auto stop = plain.find_first_of(" .,?!", begin);
    const auto size = unknown ? (stop == std::string::npos ? plain.size() : stop) - begin : piece.size();
    if (begin == std::string::npos) {
      throw std::logic_error("test: no token '" + token + "' in '" + plain + "'");
    }
    tokens.push_back({static_cast<int64_t>(tokens.size()), {begin, size}, unknown});
    pos = begin + size;
  }
  return tokens;
}

inline VoiceConfig TestVoice() {
  return ReadVoiceConfig(CreateDataReader("data/"), "vits/synthetic/voice.json");
}

} // namespace giga_edge::vits::test
