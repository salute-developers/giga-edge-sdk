#include "giga_edge/modules/tts/interpreter/backends/vits/text/morph_dict.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"
#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace giga_edge::vits {

namespace {

constexpr size_t kHeaderSize = 24;
constexpr size_t kChecksumSize = 4;
constexpr uint32_t kVersion = 1;
constexpr size_t kTagSize = 2;

// 0 for a symbol outside the dictionary alphabet; 'ё' is 'е'.
uint8_t LetterCode(char32_t letter) {
  if (letter == U'-') {
    return 1;
  }
  if (letter == U'ё') {
    letter = U'е';
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

WordForm MorphDict::Analyses::operator[](size_t index) const {
  if (index >= size_) {
    throw std::out_of_range("vits: morph dictionary: no analysis " + std::to_string(index));
  }
  const unsigned tag = tags_[kTagSize * index] | (static_cast<unsigned>(tags_[kTagSize * index + 1]) << 8);
  constexpr unsigned parts = 3;
  constexpr unsigned cases = 7;
  constexpr unsigned numbers = 3;
  constexpr unsigned unused = 0xE000;
  const auto part = tag & 3U;
  const auto grammatical_case = (tag >> 2) & 7U;
  const auto gender = (tag >> 5) & 3U;
  const auto number = (tag >> 7) & 3U;
  if (part >= parts || grammatical_case >= cases || number >= numbers || (tag & unused) != 0) {
    return {};
  }
  constexpr PartOfSpeech part_list[] = {PartOfSpeech::NOUN, PartOfSpeech::ADJECTIVE, PartOfSpeech::VERB};
  constexpr Gender gender_list[] = {Gender::MASCULINE, Gender::MASCULINE, Gender::FEMININE, Gender::NEUTER};
  WordForm form;
  form.part = part_list[part];
  form.reading.gender = gender_list[gender];
  form.reading.plural = number == 2;
  form.reading.grammatical_case = static_cast<GrammaticalCase>(grammatical_case == 0 ? 0 : grammatical_case - 1);
  // The accusative of the animate is its genitive in the plural and in the masculine singular: "гостя", "гостей".
  form.reading.animate = (tag & (1U << 9)) != 0 && form.reading.grammatical_case == GrammaticalCase::ACCUSATIVE
    && (form.reading.plural || gender == 1);
  form.indeclinable = (tag & (1U << 10)) != 0;
  form.plural_only = (tag & (1U << 11)) != 0;
  form.transitive = (tag & (1U << 12)) != 0;
  if (grammatical_case == 0 && form.part == PartOfSpeech::NOUN) {
    // A noun without a case is not a reading of a number's noun.
    form.part = PartOfSpeech::OTHER;
  }
  return form;
}

MorphDict::MorphDict(IDataSpanPtr data, const std::string& name, bool verify)
  : span_(std::move(data)) {
  const auto fail = [&name](const std::string& what) {
    throw std::runtime_error("vits: morph dictionary '" + name + "': " + what);
  };
  if (!span_) {
    fail("no data");
  }
  const auto [bytes, size] = span_->Get();
  const auto* file = reinterpret_cast<const uint8_t*>(bytes);
  if (size < kHeaderSize + kChecksumSize) {
    fail("the file is truncated: " + std::to_string(size) + " bytes");
  }
  if (std::memcmp(file, "GEMD", 4) != 0) {
    fail("not a morph dictionary");
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

MorphDict::Block MorphDict::GetBlock(size_t index) const {
  const size_t begin = ReadU32(offsets_ + 4 * index);
  const size_t end = index + 1 < blocks_ ? ReadU32(offsets_ + 4 * (index + 1)) : data_size_;
  if (begin > end || end > data_size_) {
    return {};
  }
  return {blocks_data_ + begin, blocks_data_ + end};
}

MorphDict::Analyses MorphDict::Find(std::string_view word) const {
  std::array<uint8_t, kMaxLetters> key{};
  size_t key_size = 0;
  for (size_t pos = 0; pos < word.size();) {
    const auto code_point = DecodeUtf8(word, pos);
    const auto code = LetterCode(code_point.value);
    if (code == 0 || key_size == kMaxLetters) {
      return {};
    }
    key[key_size++] = code;
    pos += code_point.size;
  }

  // The last block whose first word is not after the key.
  size_t low = 0;
  size_t high = blocks_;
  while (low < high) {
    const auto middle = low + (high - low) / 2;
    const auto block = GetBlock(middle);
    const auto size = static_cast<size_t>(block.end - block.begin);
    if (size < 2 || block.begin[1] > size - 2) {
      return {};
    }
    if (Compare(block.begin + 2, block.begin[1], key.data(), key_size) <= 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low == 0) {
    return {};
  }

  const auto block = GetBlock(low - 1);
  std::array<uint8_t, kMaxLetters> current{};
  size_t current_size = 0;
  for (const auto* pos = block.begin; block.end - pos >= 2;) {
    const size_t shared = pos[0];
    const size_t rest = pos[1];
    pos += 2;
    // A word sharing more letters than the one before reads as a word with letters 0, never found.
    if (shared + rest > kMaxLetters || rest >= static_cast<size_t>(block.end - pos)) {
      return {};
    }
    std::memcpy(current.data() + shared, pos, rest);
    current_size = shared + rest;
    pos += rest;
    const size_t count = *pos++;
    if (count == 0 || kTagSize * count > static_cast<size_t>(block.end - pos)) {
      return {};
    }
    const int order = Compare(current.data(), current_size, key.data(), key_size);
    if (order == 0) {
      Analyses analyses;
      analyses.tags_ = pos;
      analyses.size_ = count;
      return analyses;
    }
    if (order > 0) {
      break;
    }
    pos += kTagSize * count;
  }
  return {};
}

WordInfo LoadMorphWordInfo(IDataReader& reader, const std::string& directory, bool verify) {
  const auto path = (directory.empty() || directory.back() == '/' ? directory : directory + "/")
    + std::string(kMorphDictFile);
  auto span = reader.ReadPersistentData(path);
  if (!span) {
    return nullptr;
  }
  const auto dictionary = std::make_shared<const MorphDict>(std::move(span), path, verify);
  return [dictionary](std::string_view word) {
    std::vector<WordForm> forms;
    const auto analyses = dictionary->Find(word);
    forms.reserve(analyses.Size());
    for (size_t index = 0; index < analyses.Size(); ++index) {
      forms.push_back(analyses[index]);
    }
    return forms;
  };
}

} // namespace giga_edge::vits
