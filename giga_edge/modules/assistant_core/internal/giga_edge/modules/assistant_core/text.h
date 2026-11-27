#pragma once

#include <string>
#include <string_view>

namespace giga_edge {

std::string Trim(std::string_view text);
std::string_view TrimView(std::string_view text);
std::string TrimTrailingNewlines(std::string_view text);
// How many bytes at the end of text match the beginning of marker.
size_t SuffixPrefixOverlap(std::string_view text, std::string_view marker);
// Decimal notation [+-]digits[.digits], without exponent, hex, nan and inf.
bool IsDecimalNumber(std::string_view text);

size_t Utf8SequenceLength(unsigned char lead);
size_t CompleteUtf8Prefix(std::string_view text);
size_t Utf8PrefixWithin(std::string_view text, size_t limit);
bool IsValidUtf8(std::string_view text);
// Invalid sequences are replaced with U+FFFD.
std::string CutUtf8(std::string_view text, size_t max_bytes);

} // namespace giga_edge
