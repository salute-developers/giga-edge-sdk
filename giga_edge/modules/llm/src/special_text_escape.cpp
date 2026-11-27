#include "giga_edge/modules/llm/special_text_escape.h"

#include "giga_edge/modules/assistant_core/text.h"

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace giga_edge {
namespace {

constexpr std::string_view kBreak = "\xE2\x80\x8B";       // U+200B
constexpr std::string_view kReplacement = "\xEF\xBF\xBD"; // U+FFFD
constexpr int kTemplate = 0;
constexpr int kForeign = -1;

// The source of each byte: the template, foreign text or the number of a verified piece.
std::vector<int> Sources(size_t size, const std::vector<TextSpan>& spans) {
  std::vector<int> sources(size, kTemplate);
  int verified = kTemplate;
  for (const auto& span : spans) {
    const int source = span.origin == TextOrigin::FOREIGN ? kForeign : ++verified;
    const auto begin = sources.begin() + static_cast<std::ptrdiff_t>(std::min(span.begin, size));
    const auto end = sources.begin() + static_cast<std::ptrdiff_t>(std::min(span.end, size));
    std::fill(begin, end, source);
  }
  return sources;
}

} // namespace

std::string EscapeSpecialTexts(
  std::string_view text, const std::vector<TextSpan>& spans, const std::vector<std::string>& special_texts
) {
  if (spans.empty() || special_texts.empty()) {
    return std::string(text);
  }
  std::array<std::vector<const std::string*>, 256> by_first_byte;
  for (const auto& special : special_texts) {
    if (!special.empty()) {
      by_first_byte[static_cast<unsigned char>(special.front())].push_back(&special);
    }
  }
  const auto sources = Sources(text.size(), spans);
  std::vector<size_t> breaks;
  std::vector<std::pair<size_t, size_t>> replaced;
  for (size_t begin = 0; begin < text.size(); ++begin) {
    for (const auto* special : by_first_byte[static_cast<unsigned char>(text[begin])]) {
      const size_t end = begin + special->size();
      if (end > text.size() || text.compare(begin, special->size(), *special) != 0) {
        continue;
      }
      const auto first = sources.begin() + static_cast<std::ptrdiff_t>(begin);
      const auto last = sources.begin() + static_cast<std::ptrdiff_t>(end);
      if (const auto boundary = std::adjacent_find(first, last, std::not_equal_to<>()); boundary != last) {
        breaks.push_back(static_cast<size_t>(boundary - sources.begin()) + 1);
        continue;
      }
      if (*first != kForeign) {
        continue;
      }
      const size_t cut = begin + Utf8SequenceLength(static_cast<unsigned char>(text[begin]));
      if (cut < end) {
        breaks.push_back(cut);
      } else {
        replaced.emplace_back(begin, end);
      }
    }
  }
  if (breaks.empty() && replaced.empty()) {
    return std::string(text);
  }
  std::sort(breaks.begin(), breaks.end());
  breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
  std::sort(replaced.begin(), replaced.end());

  std::string out;
  out.reserve(text.size() + breaks.size() * kBreak.size());
  auto next_break = breaks.begin();
  auto next_replaced = replaced.begin();
  for (size_t i = 0; i < text.size();) {
    if (next_break != breaks.end() && *next_break == i) {
      out += kBreak;
      ++next_break;
    }
    if (next_replaced != replaced.end() && next_replaced->first == i) {
      out += kReplacement;
      i = next_replaced->second;
      while (next_replaced != replaced.end() && next_replaced->first < i) {
        ++next_replaced;
      }
      while (next_break != breaks.end() && *next_break < i) {
        ++next_break;
      }
      continue;
    }
    out += text[i++];
  }
  return out;
}

std::string CheckSpecialTexts(const std::vector<std::string>& special_texts) {
  for (const auto& special : special_texts) {
    if (!IsValidUtf8(special)) {
      return "special token text is not valid UTF-8";
    }
    if (special.find(kBreak) != std::string::npos || special.find(kReplacement) != std::string::npos) {
      return "special token text contains U+200B or U+FFFD: '" + special + "'";
    }
  }
  return "";
}

} // namespace giga_edge
