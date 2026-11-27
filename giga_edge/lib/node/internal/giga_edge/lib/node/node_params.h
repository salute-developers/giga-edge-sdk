#pragma once

#include "yaml-cpp/yaml.h"

#include <string>
#include <unordered_map>

namespace giga_edge {

struct CommonNodeParams {
  int statistics_length;
};

struct NodeParams {
  std::string type;

  std::string name;

  std::unordered_map<std::string, std::string> inputs;
  std::unordered_map<std::string, std::string> outputs;

  int priority;

  YAML::Node params;
};

} // namespace giga_edge