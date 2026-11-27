#include "giga_edge/modules/spotter/nodes/extract_event_hotword/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum_fwd.h"

namespace giga_edge {

template<>
ExtractEventHotwordNodeParams ParseConfig(const YAML::Node& config) {
  ExtractEventHotwordNodeParams result;
  result.event_type = EnumFromString<SpotterEventType>(config["event_type"].as<std::string>("activation"));
  result.asr_variable = config["asr_variable"].as<std::string>("");
  for (const auto& form : config["asr_forms"]) {
    result.asr_forms[form.first.as<std::string>()] = form.second.as<std::string>();
  }
  return result;
}

} // namespace giga_edge
