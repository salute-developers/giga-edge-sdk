#include "giga_edge/modules/tts/interpreter/backends/vits/text/stress_dict.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/text/letters.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace giga_edge::vits {

namespace {

constexpr size_t kHeaderSize = 24;
constexpr size_t kChecksumSize = 4;
constexpr uint32_t kVersion = 1;
constexpr uint8_t kMoreMarks = 0x80;

// 0 for a symbol outside the dictionary alphabet.
uint8_t LetterCode(char32_t letter) {
  if (letter == U'-') {
    return 1;
  }
  if (letter == U'ё') {
    return 8;
  }
  if (letter >= U'а' && letter <= U'е') {
    return static_cast<uint8_t>(2 + (letter - U'а'));
  }
  if (letter >= U'ж' && letter <= U'я') {
    return static_cast<uint8_t>(9 + (letter - U'ж'));
  }
  return 0;
}

int Compare(const uint8_t* left, size_t left_size, const uint8_t* right, size_t right_size) {
  const int order = std::memcmp(left, right, std::min(left_size, right_size));
  if (order != 0) {
    return order;
  }
  return left_size < right_size ? -1 : (left_size > right_size ? 1 : 0);
}

} // namespace

StressDict::StressDict(IDataSpanPtr data, const std::string& name, bool verify)
  : span_(std::move(data)) {
  const auto fail = [&name](const std::string& what) {
    throw std::runtime_error("vits: word dictionary '" + name + "': " + what);
  };
  if (!span_) {
    fail("no data");
  }
  const auto [bytes, size] = span_->Get();
  const auto* file = reinterpret_cast<const uint8_t*>(bytes);
  if (size < kHeaderSize + kChecksumSize) {
    fail("the file is truncated: " + std::to_string(size) + " bytes");
  }
  if (std::memcmp(file, "GEWD", 4) != 0) {
    fail("not a word dictionary");
  }
  if (const auto version = ReadU32(file + 4); version != kVersion) {
    fail("unsupported version " + std::to_string(version) + ", expected " + std::to_string(kVersion));
  }
  words_ = ReadU32(file + 8);
  blocks_ = ReadU32(file + 12);
  const uint64_t block_words = ReadU32(file + 16);
  data_size_ = ReadU32(file + 20);
  const uint64_t expected = kHeaderSize + 4 * static_cast<uint64_t>(blocks_) + data_size_ + kChecksumSize;
  if (expected != size) {
    fail(
      "the file is truncated or damaged: " + std::to_string(size) + " bytes, the header describes "
      + std::to_string(expected)
    );
  }
  if (block_words == 0 || (words_ + block_words - 1) / block_words != blocks_) {
    fail("the header is damaged");
  }
  if (verify && Adler32(file, size - kChecksumSize) != ReadU32(file + size - kChecksumSize)) {
    fail("the file is damaged: wrong checksum");
  }
  offsets_ = file + kHeaderSize;
  blocks_data_ = offsets_ + 4 * blocks_;
}

StressDict::Block StressDict::GetBlock(size_t index) const {
  const size_t begin = ReadU32(offsets_ + 4 * index);
  const size_t end = index + 1 < blocks_ ? ReadU32(offsets_ + 4 * (index + 1)) : data_size_;
  if (begin > end || end > data_size_) {
    return {};
  }
  return {blocks_data_ + begin, blocks_data_ + end};
}

std::optional<StressDict::Marks> StressDict::Find(std::string_view word) const {
  std::array<uint8_t, kMaxLetters> key{};
  size_t key_size = 0;
  for (size_t pos = 0; pos < word.size();) {
    uint8_t code = 0;
    if (word[pos] == '-') {
      code = 1;
      pos += 1;
    } else if (IsTwoByteSymbol(word, pos)) {
      code = LetterCode(LetterAt(word.substr(pos), 0));
      pos += kLetterBytes;
    }
    if (code == 0 || key_size == kMaxLetters) {
      return std::nullopt;
    }
    key[key_size++] = code;
  }

  // The last block whose first word is not after the key.
  size_t low = 0;
  size_t high = blocks_;
  while (low < high) {
    const auto middle = low + (high - low) / 2;
    const auto block = GetBlock(middle);
    const auto size = static_cast<size_t>(block.end - block.begin);
    if (size < 2 || block.begin[1] > size - 2) {
      return std::nullopt;
    }
    if (Compare(block.begin + 2, block.begin[1], key.data(), key_size) <= 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low == 0) {
    return std::nullopt;
  }

  const auto block = GetBlock(low - 1);
  std::array<uint8_t, kMaxLetters> current{};
  size_t current_size = 0;
  for (const auto* pos = block.begin; block.end - pos >= 2;) {
    const size_t shared = pos[0];
    const size_t rest = pos[1];
    pos += 2;
    if (shared > current_size || shared + rest > kMaxLetters || rest >= static_cast<size_t>(block.end - pos)) {
      return std::nullopt;
    }
    std::memcpy(current.data() + shared, pos, rest);
    current_size = shared + rest;
    pos += rest;
    Marks marks;
    for (bool more = true; more; ++pos) {
      if (pos == block.end) {
        return std::nullopt;
      }
      marks.set(*pos & ~kMoreMarks);
      more = (*pos & kMoreMarks) != 0;
    }
    const int order = Compare(current.data(), current_size, key.data(), key_size);
    if (order == 0) {
      return marks;
    }
    if (order > 0) {
      break;
    }
  }
  return std::nullopt;
}

} // namespace giga_edge::vits
