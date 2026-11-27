#include "giga_edge/modules/common/nodes/timer/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(TimerNodeParams::RestartStrategy, "restart", "ignore");

namespace giga_edge {

template<>
TimerNodeParams ParseConfig(const YAML::Node& config) {
  TimerNodeParams result;

  result.restart_strategy = EnumFromString<TimerNodeParams::RestartStrategy>(
    config["restart_strategy"].as<std::string>(std::string{EnumToString(result.restart_strategy)})
  );

  result.continuous_alarm = config["continuous_alarm"].as<bool>(result.continuous_alarm);

  result.timer = config["timer"].as<int32_t>();
  return result;
}

} // namespace giga_edge