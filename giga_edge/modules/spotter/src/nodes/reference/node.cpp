#include "giga_edge/modules/spotter/nodes/reference/node.h"

namespace giga_edge {

ReferenceEventsNode::ReferenceEventsNode(const NodeBaseConfig& base_config, const ReferenceEventsNodeParams& params)
  : NodeBase(base_config)
  , params_(params) {
  reference_activation_timeout_.assign(params_.reference_timer.size(), 0);
  reference_activation_scores_.assign(params_.reference_timer.size(), 0);
  reference_model_names_.resize(params_.reference_timer.size());
}

void ReferenceEventsNode::ProcessOnce() {
  auto events = GetEvents();
  auto reference_events = GetReferenceEvents();

  std::unordered_map<std::string_view, std::vector<size_t>> event_positions;
  for (size_t i = 0; i < events.size(); ++i) {
    event_positions[events[i][0].model_name].push_back(i);
  }

  for (auto& reference_event : reference_events) {
    for (int i = 0; i < reference_event.size(); ++i) {
      if (reference_event[i].event_type == SpotterEventType::ACTIVATION) {
        reference_activation_timeout_[i] = params_.reference_timer[i];
        reference_activation_scores_[i] = reference_event[i].score;
        reference_model_names_[i] = reference_event[i].model_name;
        reference_event[i].event_type = SpotterEventType::SUBACTIVATION;
      } else if (reference_event[i].event_type == SpotterEventType::SUBACTIVATION) {
      } else if (reference_activation_timeout_[i]) {
        --reference_activation_timeout_[i];
      }
    }
  }

  for (auto& event : events) {
    for (int i = 0; i < event.size(); ++i) {
      if (
        (event[i].event_type == SpotterEventType::ACTIVATION || event[i].event_type == SpotterEventType::SUBACTIVATION)
        && reference_activation_timeout_[i]
      ) {
        event[i].event_type = SpotterEventType::SUBACTIVATION;
        event[i].model_name += "#" + reference_model_names_[i];
        event[i].score = reference_activation_scores_[i];
      }
    }
  }

  for (auto& reference_event : reference_events) {
    for (auto& single_event : reference_event) {
      if (single_event.event_type == SpotterEventType::SUBACTIVATION) {
        events.push_back(std::move(reference_event));
        break;
      }
    }
  }

  PushEvents(std::move(events));
}

void ReferenceEventsNode::Reset() {
  reference_activation_timeout_.assign(reference_activation_timeout_.size(), 0);
}

} // namespace giga_edge
