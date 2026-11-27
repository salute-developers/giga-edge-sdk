#pragma once

#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/lib/data_reader/helpers.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace giga_edge::vits::test {

inline constexpr char kTextDirectory[] = "vits/text/ru";

// A file of the test data as it is.
inline std::string ReadData(const std::string& path) {
  return ToString(EnsureNotNull(CreateDataReader("data/")->ReadTransientData(path), path));
}

inline IDataSpanPtr MakeSpan(const std::string& bytes) {
  return Create<IDataSpanPtr>(std::vector<char>(bytes.begin(), bytes.end()));
}

// The files of the test text package in memory. `changes` replaces a file or, with no value, takes it away.
inline IDataReaderPtr PackageReader(const std::map<std::string, std::optional<std::string>>& changes = {}) {
  const auto disk = CreateDataReader("data/");
  std::unordered_map<std::string, IDataSpanPtr> cache;
  for (const char* name :
       {"stress.dict",
        "yo.dict",
        "yo_extra.tsv",
        "homographs.tsv",
        "stress_overrides.tsv",
        "hyphen.tsv",
        "no_pause_words.txt",
        "e_to_eh.tsv",
        "stress.onnx",
        "stress_vocab.txt",
        "homographs.onnx",
        "homographs_vocab.txt",
        "homographs.dict"}) {
    const auto path = std::string(kTextDirectory) + "/" + name;
    if (changes.count(name) == 0) {
      cache.emplace(path, EnsureNotNull(disk->ReadPersistentData(path), path));
    }
  }
  for (const auto& [name, content] : changes) {
    if (content.has_value()) {
      cache.emplace(std::string(kTextDirectory) + "/" + name, MakeSpan(*content));
    }
  }
  return CreateCachingDataReader(nullptr, std::move(cache));
}

inline void AppendU32(std::string& bytes, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes += static_cast<char>((value >> shift) & 0xFF);
  }
}

inline uint32_t Adler32(const std::string& bytes) {
  uint32_t low = 1;
  uint32_t high = 0;
  for (const char byte : bytes) {
    low = (low + static_cast<unsigned char>(byte)) % 65521;
    high = (high + low) % 65521;
  }
  return (high << 16) | low;
}

// A word dictionary file in the format of stress_dict.h, written by the tests themselves. Words: UTF-8 with '+'
// before the marked letters.
inline std::string BuildDict(const std::vector<std::string>& marked_words, uint32_t block_words = 16) {
  static const std::string alphabet = "-абвгдеёжзийклмнопрстуфхцчшщъыьэюя";
  std::map<std::string, std::string> words; // letter codes -> marks
  for (const auto& marked : marked_words) {
    std::string codes;
    std::string marks;
    for (size_t pos = 0; pos < marked.size();) {
      if (marked[pos] == '+') {
        marks += static_cast<char>(codes.size());
        pos += 1;
        continue;
      }
      const size_t size = marked[pos] == '-' ? 1 : 2;
      const auto at = alphabet.find(marked.substr(pos, size));
      codes += static_cast<char>(at == 0 ? 1 : (at + 1) / 2 + 1);
      pos += size;
    }
    for (size_t i = 0; i + 1 < marks.size(); ++i) {
      marks[i] = static_cast<char>(marks[i] | 0x80);
    }
    words[codes] = marks;
  }
  std::string data;
  std::vector<uint32_t> offsets;
  std::string previous;
  uint32_t in_block = 0;
  for (const auto& [codes, marks] : words) {
    if (in_block == 0) {
      offsets.push_back(static_cast<uint32_t>(data.size()));
      previous.clear();
    }
    size_t shared = 0;
    while (shared < previous.size() && shared < codes.size() && previous[shared] == codes[shared]) {
      ++shared;
    }
    data += static_cast<char>(shared);
    data += static_cast<char>(codes.size() - shared);
    data += codes.substr(shared) + marks;
    previous = codes;
    in_block = (in_block + 1) % block_words;
  }
  std::string file = "GEWD";
  AppendU32(file, 1);
  AppendU32(file, static_cast<uint32_t>(words.size()));
  AppendU32(file, static_cast<uint32_t>(offsets.size()));
  AppendU32(file, block_words);
  AppendU32(file, static_cast<uint32_t>(data.size()));
  for (const auto offset : offsets) {
    AppendU32(file, offset);
  }
  file += data;
  AppendU32(file, Adler32(file));
  return file;
}

} // namespace giga_edge::vits::test
