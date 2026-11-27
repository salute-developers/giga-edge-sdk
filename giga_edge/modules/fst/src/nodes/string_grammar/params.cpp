#include "giga_edge/modules/fst/nodes/string_grammar/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
StringGrammarNodeParams ParseConfig(const YAML::Node& config) {
  StringGrammarNodeParams result;
  result.fst_rule = config["fst_rule"].as<std::string>();
  return result;
}

} // namespace giga_edge