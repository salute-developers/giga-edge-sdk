#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <stdexcept>

namespace giga_edge::vits {
namespace {

bool IsSpace(char32_t ch) {
  return ch == U' ' || (ch >= U'\t' && ch <= U'\r');
}

bool IsPunctuation(char32_t ch) {
  return (ch >= U'!' && ch <= U'/') || (ch >= U':' && ch <= U'@') || (ch >= U'[' && ch <= U'`')
    || (ch >= U'{' && ch <= U'~');
}

} // namespace

WordPiece::WordPiece(std::string_view vocab, size_t max_word_length)
  : max_word_length_(max_word_length) {
  int64_t id = 0;
  for (size_t begin = 0; begin < vocab.size();) {
    auto end = vocab.find('\n', begin);
    end = end == std::string_view::npos ? vocab.size() : end;
    auto token = vocab.substr(begin, end - begin);
    if (!token.empty() && token.back() == '\r') {
      token.remove_suffix(1);
    }
    if (!token.empty()) {
      ids_[std::string(token)] = id++;
    }
    begin = end + 1;
  }
  const auto unknown = ids_.find("[UNK]");
  if (unknown == ids_.end()) {
    throw std::runtime_error("vits: the BERT vocabulary has no [UNK]");
  }
  unknown_id_ = unknown->second;
}

std::optional<int64_t> WordPiece::Find(std::string_view token) const {
  const auto found = ids_.find(std::string(token));
  return found == ids_.end() ? std::nullopt : std::optional<int64_t>(found->second);
}

std::vector<WordPieceToken> WordPiece::Tokenize(std::string_view text) const {
  std::vector<WordPieceToken> tokens;
  size_t word = std::string_view::npos;
  for (size_t pos = 0; pos < text.size();) {
    const auto symbol = DecodeUtf8(text, pos);
    const bool space = IsSpace(symbol.value);
    const bool punctuation = IsPunctuation(symbol.value);
    if ((space || punctuation) && word != std::string_view::npos) {
      AddWord(text, word, pos, tokens);
      word = std::string_view::npos;
    }
    if (punctuation) {
      AddWord(text, pos, pos + symbol.size, tokens);
    } else if (!space && word == std::string_view::npos) {
      word = pos;
    }
    pos += symbol.size;
  }
  if (word != std::string_view::npos) {
    AddWord(text, word, text.size(), tokens);
  }
  return tokens;
}

void WordPiece::AddWord(std::string_view text, size_t begin, size_t end, std::vector<WordPieceToken>& tokens) const {
  // Code point boundaries of the word, no more than a word of one code point over the limit needs.
  std::vector<size_t> bounds;
  for (size_t pos = begin; pos < end && bounds.size() <= max_word_length_; pos += DecodeUtf8(text, pos).size) {
    bounds.push_back(pos);
  }
  bounds.push_back(end);
  const auto first = tokens.size();
  std::string piece;
  for (size_t start = 0; start + 1 < bounds.size() && bounds.size() <= max_word_length_ + 1;) {
    auto stop = bounds.size() - 1;
    for (; stop > start; --stop) {
      piece.assign(start == 0 ? "" : "##").append(text.substr(bounds[start], bounds[stop] - bounds[start]));
      const auto found = ids_.find(piece);
      if (found != ids_.end()) {
        tokens.push_back({found->second, {bounds[start], bounds[stop] - bounds[start]}, false});
        break;
      }
    }
    if (stop == start) {
      tokens.resize(first);
      break;
    }
    start = stop;
  }
  if (tokens.size() == first) {
    tokens.push_back({unknown_id_, {begin, end - begin}, true});
  }
}

} // namespace giga_edge::vits
