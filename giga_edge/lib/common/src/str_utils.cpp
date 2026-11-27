#include "giga_edge/lib/common/str_utils.h"

#include <algorithm>
#include <codecvt>
#include <iostream>
#include <locale>

namespace giga_edge {

std::vector<std::string_view> SplitString(std::string_view str, const char delimeter) {
  std::vector<std::string_view> result;

  for (auto pos = str.find(delimeter); pos != std::string_view::npos; pos = str.find(delimeter)) {
    result.push_back(str.substr(0, pos));
    str = str.substr(pos + 1);
  }
  result.push_back(str);
  return result;
}

void SplitStringTo(const std::string_view& str, const char delimeter, std::string& dst) {
  auto pos = str.find(delimeter);
  if (pos != std::string_view::npos) {
    std::stringstream ss;
    ss << "Cannot split string " << str << ". It has delimeters " << delimeter << " but only one destination";
    throw std::invalid_argument(ss.str());
  }
  dst = str;
}

std::vector<float> Split(const std::string_view s) {
  std::vector<float> result;
  const char* pos = s.data();
  const char* const end = pos + s.size();
  while (pos < end) {
    char* next = nullptr;
    const float value = std::strtof(pos, &next);
    if (next == pos) {
      break;
    }
    result.push_back(value);
    pos = next;
  }
  return result;
}

std::string ToUtf8(const std::u16string& str) {
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf_converter;
  try {
    return utf_converter.to_bytes(str);
  } catch (const std::range_error& e) {
    std::cout << "ERROR: Failed to convert utf16 string to utf8" << std::endl;
    return {};
  }
}

std::u16string ToUtf16(const std::string& str) {
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> utf_converter;
  try {
    return utf_converter.from_bytes(str);
  } catch (const std::range_error& e) {
    std::cerr << "ERROR: Failed to convert utf8 string to utf16" << std::endl;
    return {};
  }
}

std::u16string ToLowerRu(const std::u16string& str) {
  std::u16string result;
  for (const char16_t& letter : str) {
    if (letter >= u'А' && letter <= u'Я') {
      result.push_back(letter + 32);
    } else if (letter == u'Ё') {
      result.push_back(u'ё');
    } else {
      result.push_back(letter);
    }
  }
  return result;
}

char16_t ToLowerLatinCyrillic(char16_t ch) {
  if (ch >= u'A' && ch <= u'Z') {
    return ch + 32;
  }
  if (ch >= u'А' && ch <= u'Я') {
    return ch + 32;
  }
  if (ch == u'Ё') {
    return u'ё';
  }
  return ch;
}

std::u16string ToLowerLatinCyrillic(const std::u16string& str) {
  std::u16string result;
  result.reserve(str.size());
  for (const char16_t& letter : str) {
    result.push_back(ToLowerLatinCyrillic(letter));
  }
  return result;
}

char16_t ToUpperLatinCyrillic(char16_t ch) {
  if (ch >= u'a' && ch <= u'z') {
    return ch - 32;
  }
  if (ch >= u'а' && ch <= u'я') {
    return ch - 32;
  }
  if (ch == u'ё') {
    return u'Ё';
  }
  return ch;
}

bool IsAlpha(char16_t ch) {
  if ((ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z')) {
    return true;
  }
  if ((ch >= u'а' && ch <= u'я') || (ch >= u'А' && ch <= u'Я')) {
    return true;
  }
  if (ch == u'Ё' || ch == u'ё') {
    return true;
  }
  return false;
}

bool IsAlphaOrBlank(char16_t ch) {
  if (IsAlpha(ch)) {
    return true;
  }
  if (ch == u' ' || ch == u'\t') {
    return true;
  }
  return false;
}

std::string NormalizeString(const std::string& input) {
  const char* whitespace = " \t\n\r\f\v";
  size_t first = input.find_first_not_of(whitespace);
  if (std::string::npos == first) {
    return "";
  }
  size_t last = input.find_last_not_of(whitespace);
  std::string input_without_spaces = input.substr(first, (last - first + 1));
  auto lower_text = ToLowerRu(ToUtf16(input_without_spaces));

  std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), [](char16_t ch) {
    return IsAlphaOrBlank(ch) ? ch : u' ';
  });

  auto result = ToUtf8(lower_text);
  return result;
}

} // namespace giga_edge
