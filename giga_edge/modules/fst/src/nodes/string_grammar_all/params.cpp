#include "giga_edge/modules/fst/nodes/string_grammar_all/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
StringGrammarAllNodeParams ParseConfig(const YAML::Node& config) {
  StringGrammarAllNodeParams result;
  result.fst_rule = config["fst_rule"].as<std::string>();
  result.max_outputs = config["max_outputs"].as<std::size_t>(result.max_outputs);
  return result;
}

} // namespace giga_edge
