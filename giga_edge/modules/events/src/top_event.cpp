#include "giga_edge/modules/events/top_event.h"

#include <algorithm>
#include <numeric>

namespace giga_edge {
namespace {
std::tuple<unsigned, float, unsigned> EventTypeStatictics(const SpotterEvent& event, SpotterEventType type) {
  return std::accumulate(
    event.begin(),
    event.end(),
    std::make_tuple(0u, 0.0f, 0u),
    [type](const auto& stats, const SingleSpotterEvent& event) -> std::tuple<unsigned, float, unsigned> {
      return {
        std::get<0>(stats) + (event.event_type == type),
        std::get<1>(stats) + (event.event_type == type ? event.score : 0.0f),
        std::get<2>(stats) + ((event.event_type == type) && event.payload.has_value())
      };
    }
  );
}
} // namespace

SpotterEvent GetTopPriorityEvent(const std::vector<SpotterEvent>& events) {
  auto it = std::max_element(std::begin(events), std::end(events), [](const SpotterEvent& l, const SpotterEvent& r) {
    for (auto event_type :
         {SpotterEventType::ACTIVATION,
          SpotterEventType::QUICK_COMMAND,
          SpotterEventType::SUBACTIVATION,
          SpotterEventType::DISABLED,
          SpotterEventType::NONE}) {
      auto [l_events_count, l_scores_sum, l_payload_count] = EventTypeStatictics(l, event_type);
      auto [r_events_count, r_scores_sum, r_payload_count] = EventTypeStatictics(r, event_type);

      // Events without an activation: the one with more recognized data ranks higher (a payload is attached to
      // them only for the sake of what was recognized).
      if ((event_type == SpotterEventType::NONE) && (l_payload_count != r_payload_count)) {
        return l_payload_count < r_payload_count;
      }

      if (l_events_count != r_events_count) {
        return l_events_count < r_events_count;
      }
      if (l_scores_sum != r_scores_sum) {
        return l_scores_sum < r_scores_sum;
      }
    }
    return false;
  });
  return *it;
}

} // namespace giga_edge
