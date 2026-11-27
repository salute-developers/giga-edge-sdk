#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace giga_edge {
class IDataReader;
} // namespace giga_edge

namespace giga_edge::vits {

// Reads one Latin word written in lower or mixed case in Russian letters, '+' before a known stress.
using LatinWordReading = std::function<std::string(std::string_view word)>;

// Latin words and abbreviations in Russian letters: a table of readings (readings.tsv: "word<TAB>reading", the word in
// lower case, Latin or Cyrillic, may hold '-', '&', '/' or an apostrophe; the reading with stress marks), then letter
// by letter for short capitals and words without vowels ("USB" -> "ю-эс-би", "МЧС" -> "эм-чэ-эс"), then the reading
// of a word (by default Transliterate).
class WordReader {
public:
  // Throws std::runtime_error on a malformed file and ReaderException on a missing one.
  WordReader(IDataReader& reader, const std::string& directory, LatinWordReading read_word = nullptr);

  // The reading from the table; empty if none. `word` is as written.
  std::string FromTable(std::string_view word) const;

  // Whether a key of the table starts with the lower-case text: "wi-" for "wi-fi".
  bool IsTablePrefix(const std::string& lower) const;

  // A Latin word: the table, the letters or the rules.
  std::string ReadLatin(std::string_view word) const;

  // A Cyrillic abbreviation in capitals ("МЧС", "ООН"): the table, the letters or the word itself in lower case.
  std::string ReadAbbreviation(std::string_view word) const;

private:
  std::unordered_map<std::string, std::string> readings_;
  std::unordered_set<std::string> prefixes_;
  bool custom_reader_ = false; // before read_word_: it is set from the reading before it is moved
  LatinWordReading read_word_;
};

// Letter names joined by hyphens: one word with the stress on the last letter for the stress stage.
std::string SpellLatin(std::string_view word);
std::string SpellCyrillic(std::string_view word);

// Lower-case Latin letters -> Russian letters by English-like rules: "coldplay" -> "колдплэй".
std::string Transliterate(std::string_view word);

} // namespace giga_edge::vits
