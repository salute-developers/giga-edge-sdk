#pragma once

#include "yaml-cpp/yaml.h"

#define PARSE_FIELD(field) result.field = config[#field].as<decltype(decltype(result)::field)>(result.field);

#define PARSE_REQUIRED_FIELD(field) result.field = config[#field].as<decltype(decltype(result)::field)>();

#define PARSE_OPTIONAL_FIELD(field, default_value)                                                                     \
  result.field = config[#field].as<decltype(decltype(result)::field)>(default_value);

namespace giga_edge {

YAML::Node ParseYaml(const char* data, size_t size);

// Thread count: an integer, or auto for the number of physical performance cores of the machine.
int ParseThreadCount(const YAML::Node& node, int fallback);

// How many CPUs the process may use right now (taskset, cpuset, cores taken away by the firmware); 0 if unknown.
int AllowedCpuCount();

template<class T>
T ParseConfig(const YAML::Node& config);

} // namespace giga_edge