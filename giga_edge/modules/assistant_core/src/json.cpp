#include "giga_edge/modules/assistant_core/json.h"

#include <memory>
#include <stdexcept>

namespace giga_edge {

bool ParseJson(std::string_view text, Json::Value& out, std::string& error) {
  Json::CharReaderBuilder builder;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  try {
    return reader->parse(text.data(), text.data() + text.size(), &out, &error);
  } catch (const Json::Exception& e) { // nesting deeper than the stack limit of jsoncpp
    error = e.what();
    return false;
  }
}

Json::Value ParseJsonOrThrow(std::string_view text) {
  Json::Value value;
  std::string error;
  if (!ParseJson(text, value, error)) {
    throw std::runtime_error("Invalid JSON: " + error);
  }
  return value;
}

namespace {

int Hex4(std::string_view text, size_t at) {
  if (at + 4 > text.size()) {
    return -1;
  }
  int value = 0;
  for (size_t i = at; i < at + 4; ++i) {
    const char c = text[i];
    const int digit = c >= '0' && c <= '9' ? c - '0'
      : c >= 'a' && c <= 'f'               ? c - 'a' + 10
      : c >= 'A' && c <= 'F'               ? c - 'A' + 10
                                           : -1;
    if (digit < 0) {
      return -1;
    }
    value = value * 16 + digit;
  }
  return value;
}

void AppendUtf8(std::string& out, unsigned code) {
  if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
  }
  out += static_cast<char>(0x80 | (code & 0x3F));
}

} // namespace

std::string KeepJsonUtf8(std::string json) {
  if (json.find("\\u") == std::string::npos) {
    return json;
  }
  std::string out;
  out.reserve(json.size());
  for (size_t i = 0; i < json.size(); ++i) {
    if (json[i] != '\\' || i + 1 == json.size()) {
      out += json[i];
      continue;
    }
    const int code = json[i + 1] == 'u' ? Hex4(json, i + 2) : -1;
    const bool high = code >= 0xD800 && code < 0xDC00;
    const int low = high && json.compare(i + 6, 2, "\\u") == 0 ? Hex4(json, i + 8) : -1;
    if (code < 0x80 || (code >= 0xD800 && code < 0xE000 && !(high && low >= 0xDC00 && low < 0xE000))) {
      // Other escapes and a lone half of a surrogate pair are kept as is.
      out += json[i];
      out += json[++i];
      continue;
    }
    AppendUtf8(out, high ? 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00) : static_cast<unsigned>(code));
    i += high ? 11 : 5;
  }
  return out;
}

std::string WriteJson(const Json::Value& value) {
  static const auto writer = [] {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = true;
    return builder;
  }();
  // jsoncpp < 1.9 does not know emitUTF8 and escapes Cyrillic in strings that have ASCII characters.
  return KeepJsonUtf8(Json::writeString(writer, value));
}

std::string JsonQuote(std::string_view text) {
  return WriteJson(Json::Value(std::string(text)));
}

} // namespace giga_edge
