#pragma once

#include <stdexcept>
#include <string>

namespace giga_edge {

class NotSupported : public std::runtime_error {
public:
  explicit NotSupported(const std::string& module_name)
    : std::runtime_error(
        "module '" + module_name + "' is not enabled in this build; rebuild with --module " + module_name
      ) {}
};

} // namespace giga_edge
