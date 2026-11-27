#include "giga_edge/modules/fst/nodes/float_grammar/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
FloatGrammarNodeParams ParseConfig(const YAML::Node& config) {
  FloatGrammarNodeParams result;
  result.fst_rule = config["fst_rule"].as<std::string>();
  return result;
}

} // namespace giga_edge