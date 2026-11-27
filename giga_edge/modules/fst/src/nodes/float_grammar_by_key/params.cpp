#include "giga_edge/modules/fst/nodes/float_grammar_by_key/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

namespace giga_edge {

template<>
FloatGrammarByKeyNodeParams ParseConfig(const YAML::Node& config) {
  FloatGrammarByKeyNodeParams result;

  for (const auto& fst_rule : config["key_to_fst_rule"]) {
    const auto key = fst_rule.first.as<std::string>();
    const auto rule = fst_rule.second.as<std::string>();

    result.key_to_fst_rule[key] = rule;
  }

  return result;
}

} // namespace giga_edge