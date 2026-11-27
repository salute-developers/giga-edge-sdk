#pragma once

#include <map>
#include <memory>
#include <string>

namespace giga_edge {

struct Environment {
  virtual ~Environment() = default;
};

using EnvironmentPtr = std::shared_ptr<Environment>;
using EnvironmentsMap = std::map<std::string, EnvironmentPtr>;

} // namespace giga_edge
