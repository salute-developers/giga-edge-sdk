#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/foreign.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits {

// English words read the Russian way: "weekend" -> "уик+энд", "bluetooth" -> "блют+ус". A word is found in the
// pronunciation dictionary and its phonemes are written in Russian letters. Unknown words try dictionary stems and
// compounds, then the package's English/name origin model. Explicit names use practical transcription. Traditional
// names use explicit package policies for capitalized words and NAME mode. Foreign-origin rules precede the
// unknown-word routes (see foreign.h); strong Slavic signs also override dictionary pronunciations. Transcribe retains
// the phoneme rules for unknown words.
//
// The file (built from CMUdict) is kept as the data reader gives it, memory-mapped by the mapping readers; a search is
// a binary one over the lines and allocates nothing. It is UTF-8 text, one entry per line, sorted by the bytes of the
// key after the header, and the checksum line ends it:
//   #vits english 7 windows 44 34 33 23 22 12 11 01 00 stress 4
//   word<TAB>chunks                     a dictionary word: one chunk per letter, separated by spaces
//   left[letter]right<TAB>chunk         a letter rule: the phonemes of a letter between these letters
//   vowels:ending<TAB>vowel             a stress rule: the stressed vowel from the end, 0 for the last one
//   @left[part]right<TAB>letters        practical transcription: 1..4 Latin letters -> Russian letters or _
//   !ending<TAB>vowel                   name stress: digit counts from the end, F selects the first vowel
//   %ngram<TAB>score                    2..4 letters with optional ^/$ boundaries: log ratio times 1000
//   %:threshold<TAB>score               English if the summed score reaches this signed threshold
//   %:initial<TAB>score                 score added for an initial capital; %:internal for any later capital
//   &word<TAB>count                     attested regular inflections with matching phonemes, 1..999
//   >part<TAB>1                         a stressed compound part; 0 preserves a whole word's English stress
//   =word<TAB>policy reading            traditional name: A capitalized, U unknown capitalized, N NAME only
//   ~left[symbol]right<TAB>letters      accented Latin letter (NFC/NFD), before folded @ rules; ASCII context
//   #adler32 0123abcd                   Adler-32 of all the bytes before this line
// A chunk has at most 8 phonemes and a dictionary word at most 128 (the reader enforces both limits).
// Traditional readings contain lowercase Russian letters, spaces/hyphens and one vowel stress mark.
// A chunk is "_" (a silent letter) or ARPAbet phonemes joined by '.': "K.S"; a vowel ends in its stress 0, 1 or 2.
// The header lists the letter windows (left and right letters, at most 9) from the widest, each inside the previous
// one, and the longest ending of the stress rules. A rule pads the word with '^' before it and '$' after it; the
// widest window found wins. A word spelled by the letter rules takes the stress of its longest ending found among the
// words of its number of vowels (7 stands for 7 and more). The phonemes are tied to the letters so that a reduced
// vowel is written by its spelling ("dragons" -> "др+эгонз") or is dropped where it has no vowel letter ("apple" ->
// "+эпл").
//
// Thread-safe: the object is not changed after construction.
class EnglishReader {
public:
  enum class Mode { WORD, NAME };

  static constexpr size_t kMaxLetters = kMaxLatinLetters; // longer words are not read: an empty answer

  // Throws std::runtime_error naming `path` on a missing file, a file of another format, a bad header, a truncated
  // file and, with `verify`, a damaged one (a wrong checksum, keys out of order, a malformed entry). `verify` reads
  // the whole file; without it a damaged middle is not noticed and gives wrong readings, but no reads out of the file.
  EnglishReader(IDataReader& data_reader, const std::string& path, bool verify = true);
  EnglishReader(IDataSpanPtr data, const std::string& name, bool verify = true);

  // As the constructor, but nullptr for a file that the reader does not have (it must support Exists).
  static std::unique_ptr<EnglishReader> Open(IDataReader& data_reader, const std::string& path, bool verify = true);

  // Latin letters and apostrophes in any case -> lowercase Russian letters with '+' before the stressed vowel (none
  // without vowels or on the Slavic route, which defers to the Russian stress stage). Latin-1/Extended-A letters fold
  // to ASCII for dictionary lookup; meaningful accents survive in practical transcription via ~ rules. Read also joins
  // nonempty hyphen-separated components, preserving each component's stress. kMaxLetters bounds the total folded
  // letters and apostrophes; invalid input or a silent component returns an empty string. NAME explicitly selects
  // practical transcription even for a dictionary word. Traditional names use = entries according to their policy: N
  // requires NAME, U also permits an unknown capitalized word, A permits any capital. Listed phrases may contain single
  // spaces. Other known words retain dictionary pronunciation. Words with diacritics use practical transcription before
  // ASCII-only foreign guesses. WORD otherwise tries regular inflections and 2..3 dictionary parts of at least 3
  // letters. Parts with two matching inflections are reliable; other splits need an English origin score. Fewer parts
  // win, then the longest shortest part. Capitalization adds weak score features only for unknown words. Foreign-origin
  // rules precede these routes. Minimal files without @ rules retain phoneme fallback; without % rules, unknown words
  // with @ rules retain practical transcription. Longest name multigraph wins, then its widest context
  // (44 34 33 23 22 12 11 01 00). Stress defaults to the penultimate vowel when the reading ends in a vowel, otherwise
  // the last; ! endings and ё override it.
  std::string Read(std::string_view word, Mode mode = Mode::WORD) const;

  // A single component with the same Latin folding -> ARPAbet phonemes separated by spaces, "W IY1 K EH2 N D", with at
  // most one primary stress, as English puts it (Read may move it: see english.cpp), by the dictionary or the letter
  // rules only. Anything else -> an empty string.
  std::string Transcribe(std::string_view word) const;

  // Whether a single folded component is in the phoneme dictionary; traditional readings do not count.
  bool Contains(std::string_view word) const;

private:
  IDataSpanPtr span_;
  std::string_view entries_; // the lines between the header and the checksum line
  std::vector<std::pair<size_t, size_t>> windows_;
  size_t stress_ending_ = 0;
};

} // namespace giga_edge::vits
