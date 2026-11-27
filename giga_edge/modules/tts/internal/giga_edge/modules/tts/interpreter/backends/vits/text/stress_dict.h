#pragma once

#include "giga_edge/lib/data_reader/span.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace giga_edge::vits {

// A read-only dictionary "word -> marked letters" over a file kept as it is (memory-mapped by the data reader):
// nothing is parsed at load and a search allocates nothing. The stress dictionary marks the stressed letters, the
// dictionary of 'ё' marks the letters 'е' that read as 'ё'.
//
// The file, version 1; integers are little-endian:
//   header, 24 bytes:
//     char[4] "GEWD"
//     u32     version
//     u32     words
//     u32     blocks
//     u32     words in a block (the last block may hold fewer)
//     u32     bytes of the block data
//   u32 x blocks  where each block starts in the block data
//   block data    the words in the ascending order of their letter codes, block after block; a word is
//     u8          letters shared with the previous word of the block (0 for the first one)
//     u8          letters that follow
//     u8 x that   letter codes: 1 '-', 2..7 'а'..'е', 8 'ё', 9..34 'ж'..'я'
//     u8 x n      marked letters, n >= 1: a position 0..126, bit 7 is set on all but the last
//   u32           Adler-32 of all the bytes before it
// A word is at most 127 letters.
class StressDict {
public:
  static constexpr size_t kMaxLetters = 127;
  using Marks = std::bitset<kMaxLetters + 1>;

  // Throws std::runtime_error naming `name` on a file of another format or version, a truncated or a damaged one.
  // Without `verify` the checksum is not read (the file is not paged in at load): a damaged file is then not
  // reported, a search in it finds nothing or a wrong answer, but stays inside the file.
  StressDict(IDataSpanPtr data, const std::string& name, bool verify = true);

  // The marked letters of a word of lower-case Russian letters and hyphens, UTF-8.
  std::optional<Marks> Find(std::string_view word) const;

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

} // namespace giga_edge::vits
