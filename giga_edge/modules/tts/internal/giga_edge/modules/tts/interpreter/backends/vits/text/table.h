#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace giga_edge::vits {

// A data table of the text package: UTF-8 lines of tab-separated fields; empty lines and lines starting with '#'
// are skipped.
struct TableRow {
  size_t line = 0; // from 1
  std::vector<std::string_view> fields;
};

inline std::vector<std::string_view> Split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  for (size_t begin = 0;;) {
    const auto end = text.find(separator, begin);
    parts.push_back(text.substr(begin, end - begin));
    if (end == std::string_view::npos) {
      return parts;
    }
    begin = end + 1;
  }
}

inline std::vector<TableRow> ReadTable(std::string_view data) {
  std::vector<TableRow> rows;
  size_t line = 0;
  for (auto text : Split(data, '\n')) {
    ++line;
    if (!text.empty() && text.back() == '\r') {
      text.remove_suffix(1);
    }
    if (!text.empty() && text.front() != '#') {
      rows.push_back({line, Split(text, '\t')});
    }
  }
  return rows;
}

[[noreturn]] inline void ThrowTableError(const std::string& name, size_t line, const std::string& what) {
  throw std::runtime_error("vits: " + name + ":" + std::to_string(line) + ": " + what);
}

} // namespace giga_edge::vits
