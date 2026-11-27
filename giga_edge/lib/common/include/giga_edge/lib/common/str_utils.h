#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge {

struct NormalizedTextTokens {
  std::string text;
  std::vector<std::string> tokens;
};

template<class C>
void JoinStringsTo(std::ostream& os, const C& container, const std::string& delimeter) {
  bool first = true;
  for (const auto& item : container) {
    if (!first) {
      os << delimeter;
    }
    os << item;
    first = false;
  }
}

template<class C, class Pred>
void JoinStringsTo(std::ostream& os, const C& container, const std::string& delimeter, const Pred& pred) {
  bool first = true;
  for (const auto& item : container) {
    if (!first) {
      os << delimeter;
    }
    os << pred(item);
    first = false;
  }
}

template<class C>
std::string JoinStrings(const C& container, const std::string& delimeter) {
  std::ostringstream os;
  JoinStringsTo(os, container, delimeter);
  return os.str();
}

template<class C, class Pred>
std::string JoinStrings(const C& container, const std::string& delimeter, const Pred& pred) {
  std::ostringstream os;
  JoinStringsTo(os, container, delimeter, pred);
  return os.str();
}

std::vector<std::string_view> SplitString(std::string_view str, const char delimeter);

inline void ReplaceAllInPlace(std::string& s, std::string_view from, std::string_view to) {
  if (from.empty()) {
    return;
  }
  for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) {
    s.replace(pos, from.size(), to);
  }
}

void SplitStringTo(const std::string_view& str, const char delimeter, std::string& dst);

template<class... Args>
void SplitStringTo(const std::string_view& str, const char delimeter, std::string& dst, Args&... args) {
  auto pos = str.find(delimeter);
  if (pos == std::string_view::npos) {
    std::stringstream ss;
    ss << "Cannot split string " << str << ". It has no delimeters " << delimeter << " but more than one destination";
    throw std::invalid_argument(ss.str());
  }
  dst = str.substr(0, pos);
  SplitStringTo(str.substr(pos + 1), delimeter, args...);
}

std::vector<float> Split(const std::string_view s);

std::string ToUtf8(const std::u16string& str);

std::u16string ToUtf16(const std::string& str);

std::u16string ToLowerRu(const std::u16string& str);

char16_t ToLowerLatinCyrillic(char16_t ch);

std::u16string ToLowerLatinCyrillic(const std::u16string& str);

char16_t ToUpperLatinCyrillic(char16_t ch);

bool IsAlpha(char16_t ch);

bool IsAlphaOrBlank(char16_t ch);

template<class CharNormalizer>
NormalizedTextTokens NormalizeTextToTokens(const std::string& input, CharNormalizer normalize_char) {
  NormalizedTextTokens result;
  std::u16string current_token;

  const auto flush_token = [&result, &current_token]() {
    if (current_token.empty()) {
      return;
    }
    result.tokens.push_back(ToUtf8(current_token));
    current_token.clear();
  };

  for (char16_t ch : ToUtf16(input)) {
    ch = normalize_char(ch);
    if (IsAlpha(ch)) {
      current_token.push_back(ch);
    } else {
      flush_token();
    }
  }
  flush_token();

  result.text = JoinStrings(result.tokens, " ");
  return result;
}

std::string NormalizeString(const std::string& input);

} // namespace giga_edge
