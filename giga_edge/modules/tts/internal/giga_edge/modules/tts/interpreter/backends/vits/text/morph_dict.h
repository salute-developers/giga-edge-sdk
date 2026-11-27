#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/span.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/lexicon.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace giga_edge::vits {

// A read-only morphological dictionary "word form -> analyses" over a file kept as it is (memory-mapped by the data
// reader): nothing is parsed at load and a search allocates nothing. Built from OpenCorpora; the built file is
// CC BY-SA, as its source.
class MorphDict {
public:
  static constexpr size_t kMaxLetters = 127;

  // The analyses of a word, a view into the file.
  class Analyses {
  public:
    size_t Size() const {
      return size_;
    }
    // Throws std::out_of_range past the end; an analysis of a damaged file reads as PartOfSpeech::OTHER.
    WordForm operator[](size_t index) const;

  private:
    friend class MorphDict;
    const uint8_t* tags_ = nullptr;
    size_t size_ = 0;
  };

  // Throws std::runtime_error naming `name` on a file of another format or version, a truncated or a damaged one.
  // Without `verify` the checksum is not read (the file is not paged in at load): a damaged file is then not
  // reported, a search in it finds nothing or a wrong answer, but stays inside the file.
  MorphDict(IDataSpanPtr data, const std::string& name, bool verify = true);

  // The analyses of a lower-case Russian word, UTF-8, 'ё' as 'е'; none for an unknown word.
  Analyses Find(std::string_view word) const;

  size_t Size() const {
    return words_;
  }

private:
  struct Block {
    const uint8_t* begin = nullptr;
    const uint8_t* end = nullptr;
  };
  Block GetBlock(size_t index) const;

  IDataSpanPtr span_;
  const uint8_t* offsets_ = nullptr;
  const uint8_t* blocks_data_ = nullptr;
  size_t words_ = 0;
  size_t blocks_ = 0;
  size_t data_size_ = 0;
};

// The file of the dictionary in the directory of the language data.
inline constexpr std::string_view kMorphDictFile = "morph.dict";

// WordInfo from morph.dict of `directory`; an empty function if there is no such file.
WordInfo LoadMorphWordInfo(IDataReader& reader, const std::string& directory, bool verify);

} // namespace giga_edge::vits
