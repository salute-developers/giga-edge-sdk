#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The byte helpers the data files of the backend share.
namespace giga_edge::vits {

// The little-endian 32-bit number at `data`, byte by byte: any alignment.
inline uint32_t ReadU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16)
    | (static_cast<uint32_t>(data[3]) << 24);
}

// Adler-32 of the bytes, as zlib computes it.
inline uint32_t Adler32(std::string_view data) {
  constexpr uint32_t modulo = 65521;
  constexpr size_t max_chunk = 5552; // the sums do not overflow 32 bits within a chunk
  uint32_t low = 1;
  uint32_t high = 0;
  while (!data.empty()) {
    const auto chunk = std::min(data.size(), max_chunk);
    for (size_t i = 0; i < chunk; ++i) {
      low += static_cast<unsigned char>(data[i]);
      high += low;
    }
    low %= modulo;
    high %= modulo;
    data.remove_prefix(chunk);
  }
  return (high << 16) | low;
}

inline uint32_t Adler32(const uint8_t* data, size_t size) {
  return Adler32(std::string_view(reinterpret_cast<const char*>(data), size));
}

// The UTF-8 bytes of a code point at the end of `text`; the code point is a valid one (at most 0x10FFFF).
inline void AppendUtf8(std::string& text, char32_t symbol) {
  if (symbol < 0x80) {
    text += static_cast<char>(symbol);
  } else if (symbol < 0x800) {
    text += static_cast<char>(0xC0 | (symbol >> 6));
    text += static_cast<char>(0x80 | (symbol & 0x3F));
  } else if (symbol < 0x10000) {
    text += static_cast<char>(0xE0 | (symbol >> 12));
    text += static_cast<char>(0x80 | ((symbol >> 6) & 0x3F));
    text += static_cast<char>(0x80 | (symbol & 0x3F));
  } else {
    text += static_cast<char>(0xF0 | (symbol >> 18));
    text += static_cast<char>(0x80 | ((symbol >> 12) & 0x3F));
    text += static_cast<char>(0x80 | ((symbol >> 6) & 0x3F));
    text += static_cast<char>(0x80 | (symbol & 0x3F));
  }
}

} // namespace giga_edge::vits
