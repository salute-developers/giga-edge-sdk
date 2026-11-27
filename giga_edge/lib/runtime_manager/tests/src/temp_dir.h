#pragma once

#include "giga_edge/lib/runtime/tests/runtime_test_utils/temp_dir.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace tests {

using TempModelDir = giga_edge::test::TempDir;

inline void WriteConfig(const std::filesystem::path& dir, const std::string& body) {
  std::filesystem::create_directories(dir);
  std::ofstream out(dir / "config.yaml");
  if (!out) {
    throw std::runtime_error("failed to open " + (dir / "config.yaml").string());
  }
  out << body;
}

} // namespace tests
