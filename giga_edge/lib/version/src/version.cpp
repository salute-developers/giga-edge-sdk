#include "giga_edge/lib/version/version.h"

#include <iostream>
#include <stdexcept>

namespace giga_edge {
namespace {

static std::vector<size_t> SplitVersion(const std::string& version) {
  std::vector<size_t> splitted_version;

  const char* delimiter = ".";

  size_t last = 0;
  size_t next = 0;
  while ((next = version.find(delimiter, last)) != std::string::npos) {
    splitted_version.push_back(std::stoi(version.substr(last, next - last)));
    last = next + 1;
  }
  splitted_version.push_back(std::stoi(version.substr(last)));
  return splitted_version;
}

} // namespace

void CheckIfCompatible(const std::string& version) {
  if (version == kUnknownVersion) {
    std::cerr << "Could not find compatible Giga Edge SDK runtime version in config, cannot continue" << std::endl;
    throw std::runtime_error("Giga Edge SDK runtime version unknown in config");
  }

  auto splitted_version = SplitVersion(version);
  if (splitted_version.size() != 2) {
    std::cerr << "Invalid version: \"" << version << "\"" << std::endl;
    throw std::runtime_error("Giga Edge SDK runtime version in config is invalid");
  }
  auto default_splitted_version = SplitVersion(kLibVersion);
  if (default_splitted_version[0] != splitted_version[0] || default_splitted_version[1] < splitted_version[1]) {
    std::cerr << "Giga Edge SDK runtime version \"" << version << "\" is not supported. Current version is \""
              << kLibVersion << "\"." << std::endl;
    throw std::runtime_error("Giga Edge SDK runtime version unsupported");
  }
}
} // namespace giga_edge