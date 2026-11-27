#include "giga_edge/modules/assistant_core/text.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace giga_edge {
namespace {

constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

bool IsUtf8ContinuationByte(unsigned char byte) {
  return (byte & 0xC0) == 0x80;
}

// 0: an invalid sequence.
size_t ValidUtf8Length(std::string_view text) {
  const auto byte = [&text](size_t i) {
    return static_cast<unsigned char>(text[i]);
  };
  const size_t length = Utf8SequenceLength(byte(0));
  if (length == 0 || length > text.size()) {
    return 0;
  }
  for (size_t i = 1; i < length; ++i) {
    if (!IsUtf8ContinuationByte(byte(i))) {
      return 0;
    }
  }
  const unsigned char lead = byte(0);
  const unsigned char second = length > 1 ? byte(1) : 0;
  const bool overlong = (length == 2 && lead < 0xC2) || (lead == 0xE0 && second < 0xA0)
    || (lead == 0xF0 && second < 0x90);
  const bool out_of_range = (lead == 0xED && second > 0x9F) || lead > 0xF4 || (lead == 0xF4 && second > 0x8F);
  return overlong || out_of_range ? 0 : length;
}

} // namespace

std::string Trim(std::string_view text) {
  return std::string(TrimView(text));
}

std::string_view TrimView(std::string_view text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

std::string TrimTrailingNewlines(std::string_view text) {
  const auto last = text.find_last_not_of("\r\n");
  return last == std::string_view::npos ? "" : std::string(text.substr(0, last + 1));
}

size_t SuffixPrefixOverlap(std::string_view text, std::string_view marker) {
  for (size_t len = std::min(text.size(), marker.size()); len > 0; --len) {
    if (text.substr(text.size() - len) == marker.substr(0, len)) {
      return len;
    }
  }
  return 0;
}

bool IsDecimalNumber(std::string_view text) {
  size_t at = !text.empty() && (text.front() == '+' || text.front() == '-') ? 1 : 0;
  const auto skip_digits = [&text, &at] {
    const size_t from = at;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])) != 0) {
      ++at;
    }
    return at > from;
  };
  if (!skip_digits()) {
    return false;
  }
  if (at < text.size() && text[at] == '.') {
    ++at;
    if (!skip_digits()) {
      return false;
    }
  }
  return at == text.size();
}

size_t Utf8SequenceLength(unsigned char lead) {
  if (lead < 0x80) {
    return 1;
  }
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  if ((lead & 0xF8) == 0xF0) {
    return 4;
  }
  return 0;
}

size_t CompleteUtf8Prefix(std::string_view text) {
  size_t start = text.size();
  size_t scanned = 0;
  while (start > 0 && scanned < 4) {
    --start;
    ++scanned;
    if (!IsUtf8ContinuationByte(static_cast<unsigned char>(text[start]))) {
      const size_t length = Utf8SequenceLength(static_cast<unsigned char>(text[start]));
      return length != 0 && start + length <= text.size() ? text.size() : start;
    }
  }
  return text.size();
}

size_t Utf8PrefixWithin(std::string_view text, size_t limit) {
  if (text.size() <= limit) {
    return text.size();
  }
  size_t end = limit;
  while (end > 0 && IsUtf8ContinuationByte(static_cast<unsigned char>(text[end]))) {
    --end;
  }
  return end;
}

bool IsValidUtf8(std::string_view text) {
  for (size_t i = 0; i < text.size();) {
    const size_t length = ValidUtf8Length(text.substr(i));
    if (length == 0) {
      return false;
    }
    i += length;
  }
  return true;
}

std::string CutUtf8(std::string_view text, size_t max_bytes) {
  std::string out;
  out.reserve(std::min(text.size(), max_bytes));
  for (size_t i = 0; i < text.size();) {
    const size_t length = ValidUtf8Length(text.substr(i));
    const auto piece = length == 0 ? kReplacementCharacter : text.substr(i, length);
    if (out.size() + piece.size() > max_bytes) {
      break;
    }
    out += piece;
    i += length == 0 ? 1 : length;
    while (length == 0 && i < text.size() && IsUtf8ContinuationByte(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
  }
  return out;
}

} // namespace giga_edge
