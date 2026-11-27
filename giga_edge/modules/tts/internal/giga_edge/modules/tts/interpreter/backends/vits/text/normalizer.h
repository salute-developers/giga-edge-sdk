#pragma once

#include "giga_edge/modules/tts/interpreter/backends/vits/text/latin.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/lexicon.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/normalized_sentence.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/sentence_splitter.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge {
class IDataReader;
} // namespace giga_edge

namespace giga_edge::vits {

// Raw Russian text -> sentences to be read aloud: numbers, dates, times, units, money and percents become words agreed
// with the words around them. The words the normalizer makes carry '+' before the stressed vowel (see numerals.h), the
// words of the text stay as written (lower-case, with their 'ё' and '+'). Tokens it does not know pass lower-cased.
// A number with its unit, a date, a time, a range is never cut unless it is longer than a sentence itself.
class TextNormalizer {
public:
  // Whether a lower-case Russian word is a word of the language: a word in capitals is then read as a word, not letter by
  // letter ("НЕ", "СТОП").
  using WordCheck = std::function<bool(std::string_view word)>;

  // Reads the language data from `directory` of the reader (see Lexicon, WordReader); throws on missing or malformed
  // data and on bad parameters. `read_word` reads a Latin word not in the table of readings (Transliterate if empty);
  // `is_word` tells words in capitals from abbreviations (besides the section "word" of words.tsv); `word_info`
  // analyses a word the lexicon does not know: the noun after a number, the word that governs it (if empty, morph.dict
  // of `directory` if there is one, its checksum not read; see LoadMorphWordInfo).
  TextNormalizer(
    IDataReader& reader,
    const std::string& directory,
    const SentenceParams& params = {},
    LatinWordReading read_word = nullptr,
    WordCheck is_word = nullptr,
    WordInfo word_info = nullptr
  );

  // Thread-safe. Sentences without letters are left out; the last one has no pause after it.
  std::vector<NormalizedSentence> Normalize(std::string_view text) const;

private:
  Lexicon lexicon_;
  WordReader word_reader_;
  SentenceParams params_;
  WordCheck is_word_;
  WordInfo word_info_;
};

} // namespace giga_edge::vits
