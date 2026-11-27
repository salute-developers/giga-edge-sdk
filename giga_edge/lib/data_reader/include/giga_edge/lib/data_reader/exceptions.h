#pragma once

#include <stdexcept>

namespace giga_edge {

class ReaderException : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

} // namespace giga_edge