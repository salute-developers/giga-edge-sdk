#pragma once

#include "json/value.h"

#include <any>
#include <string>
#include <unordered_map>
#include <vector>

namespace giga_edge {

enum class SpotterEventType {
  NONE = 0,
  SUBACTIVATION = 1,
  ACTIVATION = 2,
  DISABLED = 3,
  QUICK_COMMAND = 4,
};

enum class SpotterEngineType {
  NONE = 0,
  SPOTTER = 1,
};

std::string EventTypeToString(const SpotterEventType& event);

struct SingleSpotterEvent {
  SpotterEventType event_type;
  std::string hot_word;
  std::string hotword_id;
  int hot_word_length_ms;
  float score;
  Json::Value latencies = Json::Value(Json::objectValue);
  std::string model_name;
  // Data recognized together with the event; its type is known to the module that produced it.
  std::any payload;
  std::vector<std::string> channels;
  float ov_threshold;
  std::unordered_map<std::string, std::string> asr_variables;
  SpotterEngineType spotter_engine;
  std::unordered_map<std::string, std::string> analytical_properties;
};

using SpotterEvent = std::vector<SingleSpotterEvent>;

} // namespace giga_edge
