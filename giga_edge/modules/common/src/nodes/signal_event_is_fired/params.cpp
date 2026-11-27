#include "giga_edge/modules/common/nodes/signal_event_is_fired/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace giga_edge {

SignalEventIsFiredResetStrategy ParseResetStrategy(const std::string& reset_strategy) {
  if (reset_strategy.empty()) {
    return SignalEventIsFiredResetStrategy::RESET_SIGNAL;
  }

  static std::unordered_map<std::string, SignalEventIsFiredResetStrategy> const table = {
    {"ignore_reset", SignalEventIsFiredResetStrategy::IGNORE_RESET},
    {"reset_signal", SignalEventIsFiredResetStrategy::RESET_SIGNAL},
  };

  auto it = table.find(reset_strategy);
  if (it != table.end()) {
    return it->second;
  } else {
    throw std::runtime_error("Unknown reset strategy " + reset_strategy);
  }
}

template<>
SignalEventIsFiredNodeParams ParseConfig(const YAML::Node& config) {
  SignalEventIsFiredNodeParams result;
  result.signal_name = config["signal_name"].as<std::string>();
  result.reset_strategy = ParseResetStrategy(config["reset_strategy"].as<std::string>(""));
  return result;
}

} // namespace giga_edge
