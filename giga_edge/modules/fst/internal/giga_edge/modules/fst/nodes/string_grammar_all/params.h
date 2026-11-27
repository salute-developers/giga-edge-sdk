#pragma once

#include <cstddef>
#include <string>

namespace giga_edge {

struct StringGrammarAllNodeParams {
  std::string fst_rule;
  std::size_t max_outputs = 32;
};

} // namespace giga_edge
