#include "giga_edge/lib/runtime/tests/runtime_test_utils/activation_matchers.h"

#include <catch.hpp>

#include <cmath>

namespace giga_edge::tests {

void CompareActivationsUpperBound(const PredictionTimes& ref, const PredictionTimes& candidate, float proximity) {
  for (const auto& [hotword, ref_times] : ref) {
    if (ref_times.empty()) {
      INFO("Candidate must not have any " << hotword << " activations\n");
      REQUIRE(candidate.find(hotword) == candidate.end());
      continue;
    }
    {
      INFO("Candidate doesn't have " << hotword << " activation\n");
      REQUIRE(candidate.find(hotword) != candidate.end());
    }
    auto candidate_times = candidate.find(hotword)->second;
    {
      INFO("Candidate and reference hit counts dont match");
      REQUIRE(candidate_times.size() == ref_times.size());
    }

    for (int i = 0; i < static_cast<int>(candidate_times.size()); ++i) {
      CAPTURE(i);
      CAPTURE(proximity);
      REQUIRE(candidate_times[i] == Approx(ref_times[i]).margin(proximity));
    }
  }
}

void RequireActivationsPresent(
  std::vector<SpotterEvent> events, const std::unordered_set<std::string>& expected_models
) {
  std::unordered_set<std::string> found_model_activations;
  for (const auto& event : events) {
    for (const auto& single_event : event) {
      if (!single_event.model_name.empty()) {
        if (single_event.model_name.find('#') == std::string::npos) { // ignore disabled by reference
          found_model_activations.insert(single_event.model_name);
        }
      }
    }
  }

  REQUIRE(found_model_activations == expected_models);
}

void RequireHotwordsPresent(
  std::vector<SpotterEvent> events, const std::unordered_set<std::string>& expected_hotwords
) {
  std::unordered_set<std::string> found_hotwords;
  for (const auto& event : events) {
    for (const auto& single_event : event) {
      if (single_event.event_type == SpotterEventType::ACTIVATION) {
        found_hotwords.insert(single_event.hot_word);
      }
    }
  }

  REQUIRE(found_hotwords == expected_hotwords);
}

} // namespace giga_edge::tests