#include "giga_edge/modules/tts/interpreter/backends/vits/acoustic/wordpiece.h"

#include <catch.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using namespace giga_edge::vits;
using Catch::Matchers::Contains;

namespace {

// Ids: [UNK] 0, вкл 1, ##ючил 2, свет 3, на 4, . 5, , 6, ? 7, ёлка 8, а 9, ##а 10, ##е 11, кухн 12 (empty lines skipped).
const WordPiece kVocab("[UNK]\nвкл\n##ючил\r\nсвет\n\nна\n.\n,\n?\nёлка\nа\n##а\n##е\nкухн\n");

// "id@begin+size" per token, "!" for [UNK].
std::string Describe(const std::vector<WordPieceToken>& tokens) {
  std::string text;
  for (const auto& token : tokens) {
    text += (text.empty() ? "" : " ") + std::to_string(token.id) + "@" + std::to_string(token.range.begin) + "+"
      + std::to_string(token.range.size) + (token.unknown ? "!" : "");
  }
  return text;
}

} // namespace

TEST_CASE("vits wordpiece: words, pieces and punctuation", "[tts][vits]") {
  CHECK(Describe(kVocab.Tokenize("включил свет на кухне.")) == "1@0+6 2@6+8 3@15+8 4@24+4 12@29+8 11@37+2 5@39+1");
  CHECK(Describe(kVocab.Tokenize("свет,на?")) == "3@0+8 6@8+1 4@9+4 7@13+1");
  CHECK(Describe(kVocab.Tokenize("  свет\t\nна\r ")) == "3@2+8 4@12+4");
  CHECK(Describe(kVocab.Tokenize("ёлка")) == "8@0+8");
  CHECK(Describe(kVocab.Tokenize("?!,")) == "7@0+1 0@1+1! 6@2+1");
  CHECK(kVocab.Tokenize("").empty());
  CHECK(kVocab.Tokenize(" \t ").empty());
}

TEST_CASE("vits wordpiece: a word that does not split is one [UNK]", "[tts][vits]") {
  // No piece at all, no continuation for "х", a letter outside the vocabulary.
  CHECK(Describe(kVocab.Tokenize("дом на")) == "0@0+6! 4@7+4");
  CHECK(Describe(kVocab.Tokenize("светх.")) == "0@0+10! 5@10+1");
  CHECK(Describe(kVocab.Tokenize("на свеtт")) == "4@0+4 0@5+9!");

  std::string longest;
  for (size_t i = 0; i < WordPiece::kMaxWordLength; ++i) {
    longest += "а";
  }
  const auto tokens = kVocab.Tokenize(longest);
  REQUIRE(tokens.size() == WordPiece::kMaxWordLength);
  CHECK(Describe({tokens.front(), tokens.back()}) == "9@0+2 10@298+2");
  CHECK(Describe(kVocab.Tokenize(longest + "а на")) == "0@0+302! 4@303+4");
}

TEST_CASE("vits wordpiece: a broken vocabulary or text throws", "[tts][vits]") {
  CHECK_THROWS_WITH(WordPiece("свет\nна\n"), Contains("no [UNK]"));
  CHECK_THROWS_WITH(WordPiece(""), Contains("no [UNK]"));
  CHECK_THROWS_WITH(kVocab.Tokenize("свет \xD1"), Contains("invalid UTF-8 at byte 9"));
  CHECK_THROWS_WITH(kVocab.Tokenize("\xC0\xAF"), Contains("invalid UTF-8 at byte 0"));
}
