#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

namespace giga_edge::test {

// Temporary directory for the duration of a test.
struct TempDir {
  std::filesystem::path path;

  // Not mkdtemp, which MSVC lacks: create_directory is atomic and returns false on a taken name.
  TempDir() {
    std::random_device random;
    do {
      path = std::filesystem::temp_directory_path() / ("giga_edge_test_" + std::to_string(random()));
    } while (!std::filesystem::create_directory(path));
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  void Write(const std::string& name, const std::string& contents) const {
    std::filesystem::create_directories((path / name).parent_path());
    std::ofstream(path / name) << contents;
  }
};

} // namespace giga_edge::test
