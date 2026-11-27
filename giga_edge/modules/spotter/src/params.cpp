#include "giga_edge/modules/spotter/params.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/enum/enum.h"

#include <iostream>

GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(HotwordGroup, "none", "main", "assistant");
GIGA_EDGE_DEFINE_SIMPLE_ENUM_MAPPING(ActivationMode, "main", "main_and_active", "all");

namespace giga_edge {

template<>
ClassMetaInfoParams ParseConfig(const YAML::Node& config) {
  ClassMetaInfoParams result;
  result.hotword = config["hotword"].as<std::string>();
  result.ru_hotword = config["ru_hotword"].as<std::string>("");
  result.hotword_length_ms = config["hotword_length_ms"].as<int>();
  result.threshold = config["threshold"].as<float>();
  result.sub_threshold = config["sub_threshold"].as<float>(result.threshold);
  result.hotword_group = EnumFromString<HotwordGroup>(config["hotword_group"].as<std::string>("none"));
  result.ov_threshold = config["ov_threshold"].as<float>(0.0);

  std::unordered_map<std::string, std::string> asr_variables;
  for (const auto& key_values : config["asr_variables"]) {
    const auto key = key_values.first.as<std::string>();
    const auto value = key_values.second.as<std::string>();

    if (key == "ov_threshold" and config["ov_threshold"].IsDefined()) {
      std::cerr
        << "[WARNING] The ov_threshold value is defined twice. The value from the asr_variables map will be used."
        << std::endl;
    } else if (key == "spotter_threshold" and config["ov_threshold"].IsDefined()) {
      std::cerr << "[WARNING] The ov_threshold value will be overwritted by spotter_threshold value." << std::endl;
    }

    asr_variables[key] = value;
  }

  result.asr_variables = std::move(asr_variables);
  return result;
}

template<>
EventGeneratorParams ParseConfig(const YAML::Node& config) {
  EventGeneratorParams result;

  result.activation_mode = EnumFromString<ActivationMode>(config["activation_mode"].as<std::string>());
  result.model_name = config["model_name"].as<std::string>("");
  result.ignore_synthesis_text = config["ignore_synthesis_text"].as<bool>(result.ignore_synthesis_text);
  result.reset_synthesis_text_delay = config["reset_synthesis_text_delay"].as<size_t>(0);

  for (const auto& channel : config["channels"]) {
    result.channels.push_back(channel.as<std::string>());
  }

  for (const auto& class_config : config["classes"]) {
    result.class_meta_infos.emplace_back(ParseConfig<ClassMetaInfoParams>(class_config));
  }
  return result;
}

template<>
SleepTimerParams ParseConfig(const YAML::Node& config) {
  SleepTimerParams result;

  for (const auto& timer : config["sleeping_timer"]) {
    result.sleeping_timer.push_back(timer.as<int>());
  }
  return result;
}

} // namespace giga_edge