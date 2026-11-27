#pragma once

#include "giga_edge/modules/spotter/spotter_event.h"

#include <unordered_set>

namespace giga_edge::tests {

using PredictionTimes = std::unordered_map<std::string, std::vector<float>>;

void CompareActivationsUpperBound(const PredictionTimes& ref, const PredictionTimes& candidate, float proximity = 0.1);

void RequireActivationsPresent(
  std::vector<SpotterEvent> events, const std::unordered_set<std::string>& expected_models
);

void RequireHotwordsPresent(std::vector<SpotterEvent> events, const std::unordered_set<std::string>& expected_hotwords);

} // namespace giga_edge::tests