#pragma once

#include <string>
#include <unordered_map>

namespace giga_edge {

struct FloatGrammarByKeyNodeParams {
  std::unordered_map<std::string, std::string> key_to_fst_rule;
};

} // namespace giga_edge
