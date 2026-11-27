#pragma once

#include "giga_edge/modules/spotter/spotter_event.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <functional>
#include <vector>

namespace giga_edge {

// Comparison of event payloads: the payload type is known to the module that produced it, so its tests pass the
// comparison.
using PayloadEquals = std::function<bool(const std::any&, const std::any&)>;

// By default neither has a payload: an event with a payload does not pass silently without its comparison.
inline bool NoPayload(const std::any& lhs, const std::any& rhs) {
  return !lhs.has_value() && !rhs.has_value();
}

inline bool Equal(const SingleSpotterEvent& lhs, const SingleSpotterEvent& rhs, const PayloadEquals& payload_equals) {
  return lhs.event_type == rhs.event_type && lhs.hot_word == rhs.hot_word
    && lhs.hot_word_length_ms == rhs.hot_word_length_ms && lhs.model_name == rhs.model_name
    && std::abs(lhs.score - rhs.score) < 1e-3 && payload_equals(lhs.payload, rhs.payload)
    && lhs.channels == rhs.channels && std::abs(lhs.ov_threshold - rhs.ov_threshold) < 1e-3
    && lhs.asr_variables == rhs.asr_variables && lhs.spotter_engine == rhs.spotter_engine
    && lhs.hotword_id == rhs.hotword_id;
}

inline bool Equal(
  const std::vector<SpotterEvent>& lhs, const std::vector<SpotterEvent>& rhs, const PayloadEquals& payload_equals
) {
  return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [&](const auto& l, const auto& r) {
    return std::equal(l.begin(), l.end(), r.begin(), r.end(), [&](const auto& a, const auto& b) {
      return Equal(a, b, payload_equals);
    });
  });
}

inline bool operator==(const SingleSpotterEvent& lhs, const SingleSpotterEvent& rhs) {
  return Equal(lhs, rhs, NoPayload);
}

} // namespace giga_edge
