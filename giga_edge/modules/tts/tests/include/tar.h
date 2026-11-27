#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

namespace giga_edge::test {

// An uncompressed tar (ustar), like the model parts on a device: the files under root/<one of files> and the
// directories leading to them; other directories are not traversed, since files of neighboring processes may appear
// and disappear in them.
inline void WriteTar(std::ostream& tar, const std::filesystem::path& root, const std::vector<std::string>& files) {
  namespace fs = std::filesystem;
  const auto add = [&tar](const std::string& name, char type, const std::string& data) {
    std::array<char, 512> header{};
    std::snprintf(header.data(), 100, "%s", name.c_str());
    std::snprintf(header.data() + 100, 8, "%07o", type == '5' ? 0755 : 0644);
    std::snprintf(header.data() + 124, 12, "%011llo", static_cast<unsigned long long>(data.size()));
    std::snprintf(header.data() + 136, 12, "%011o", 0);
    header[156] = type;
    std::memcpy(header.data() + 257, "ustar", 6);
    std::memcpy(header.data() + 263, "00", 2);
    std::memset(header.data() + 148, ' ', 8);
    unsigned sum = 0;
    for (const char c : header) {
      sum += static_cast<unsigned char>(c);
    }
    std::snprintf(header.data() + 148, 8, "%06o", sum);
    tar.write(header.data(), header.size());
    tar << data << std::string((512 - data.size() % 512) % 512, '\0');
  };
  for (auto entry = fs::recursive_directory_iterator(root); entry != fs::recursive_directory_iterator(); ++entry) {
    const auto name = fs::relative(entry->path(), root).generic_string();
    const auto listed = [&name](const std::string& prefix) {
      return name.starts_with(prefix + "/");
    };
    const auto on_the_way = [&name](const std::string& prefix) {
      return (prefix + "/").starts_with(name + "/");
    };
    if (entry->is_directory()) {
      if (std::ranges::none_of(files, listed) && std::ranges::none_of(files, on_the_way)) {
        entry.disable_recursion_pending();
        continue;
      }
      add(name + "/", '5', {});
    } else if (std::ranges::any_of(files, listed)) {
      std::ifstream in(entry->path(), std::ios::binary);
      add(name, '0', {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()});
    }
  }
  tar << std::string(1024, '\0');
}

} // namespace giga_edge::test
