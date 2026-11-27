#include "giga_edge/modules/spotter/event_generator.h"

#include "giga_edge/lib/common/str_utils.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <sstream>
#include <tuple>

namespace giga_edge {

SpotterEventGenerator::SpotterEventGenerator(const EventGeneratorParams& params)
  : params_(params) {
  for (const auto& class_meta_info : params_.class_meta_infos) {
    class_meta_infos_.emplace_back(class_meta_info, params_.activation_mode);
  }
  spotter_state_.activation_mode = params_.activation_mode;
}

std::vector<SpotterEvent> SpotterEventGenerator::GenerateSpotterEvents(
  const std::vector<float>& predictions, std::function<Json::Value()> latencies_callback
) {
  std::vector<SpotterEvent> recognition_results;

  if (predictions.empty()) {
    return {};
  }

  recognition_results.reserve(predictions.size());

  if (predictions.size() % class_meta_infos_.size() != 0) {
    throw std::runtime_error(
      "SpotterEventGenerator: predictions number is not matching classes number. "
      "Predictions: " + std::to_string(predictions.size()) + ", "
      "classes: " + std::to_string(class_meta_infos_.size())
    );
  }

  // One prediction is a score per class.
  for (int i = 0; i < static_cast<int>(predictions.size()); i += class_meta_infos_.size()) {
    SpotterEvent spotter_event;
    for (int j = 0; j < class_meta_infos_.size(); ++j) {
      int index = i + j;
      auto current_class_metainfo = class_meta_infos_[j];

      SpotterEventType event_type = SpotterEventType::NONE;

      if (predictions[index] >= current_class_metainfo.GetThreshold()) {
        if (current_class_metainfo.IsEnabled()) {
          event_type = SpotterEventType::ACTIVATION;
        } else {
          event_type = SpotterEventType::DISABLED;
        }
      } else if (predictions[index] >= current_class_metainfo.GetSubThreshold() && current_class_metainfo.IsEnabled()) {
        event_type = SpotterEventType::SUBACTIVATION;
      }

      Json::Value latency_statistics(Json::objectValue);
      if (event_type == SpotterEventType::SUBACTIVATION || event_type == SpotterEventType::ACTIVATION) {
        latency_statistics = latencies_callback();
      }
      spotter_event.push_back(
        SingleSpotterEvent{
          .event_type = event_type,
          .hot_word = current_class_metainfo.GetHotword(),
          .hot_word_length_ms = current_class_metainfo.GetHotwordLengthMs(),
          .score = predictions[index],
          .latencies = latency_statistics,
          .model_name = params_.model_name,
          .channels = params_.channels,
          .ov_threshold = current_class_metainfo.GetOvThreshold(),
          .asr_variables = current_class_metainfo.GetAsrVariables(),
          .spotter_engine = SpotterEngineType::SPOTTER,
        }
      );
    }

    recognition_results.emplace_back(std::move(spotter_event));
  }
  return recognition_results;
}

std::vector<IClassMetaInfoPtr> SpotterEventGenerator::GetClassMetaInfos() const {
  std::vector<IClassMetaInfoPtr> meta_info_ptrs;
  meta_info_ptrs.reserve(class_meta_infos_.size());
  for (const auto& class_meta_info : class_meta_infos_) {
    meta_info_ptrs.push_back(std::make_unique<ClassMetaInfo>(class_meta_info));
  }
  return meta_info_ptrs;
}

SpotterState SpotterEventGenerator::GetState() const {
  return spotter_state_;
}

const EventGeneratorParams& SpotterEventGenerator::GetParams() const {
  return params_;
}

void SpotterEventGenerator::SetActiveName(const std::string& active_name) {
  spotter_state_.active_name = active_name;
  ApplySpotterStateChanges();
}

void SpotterEventGenerator::SetActivationMode(ActivationMode activation_mode) {
  spotter_state_.activation_mode = activation_mode;
  ApplySpotterStateChanges();
}

void SpotterEventGenerator::ApplySpotterStateChanges() {
  switch (spotter_state_.activation_mode) {
    case ActivationMode::MAIN:
      for (auto& class_meta : class_meta_infos_) {
        class_meta.SetEnabled(class_meta.GetHotwordGroup() == HotwordGroup::MAIN);
      }
      break;
    case ActivationMode::MAIN_AND_ACTIVE:
      for (auto& class_meta : class_meta_infos_) {
        class_meta.SetEnabled(
          class_meta.GetHotwordGroup() == HotwordGroup::MAIN
          || (class_meta.GetHotwordGroup() == HotwordGroup::ASSISTANT && class_meta.GetHotword() == spotter_state_.active_name)
        );
      }
      break;
    case ActivationMode::ALL:
      for (auto& class_meta : class_meta_infos_) {
        class_meta.SetEnabled(true);
      }
  }
}

void SpotterEventGenerator::SetSynthesisText(const std::string& synthesis_text, bool force) {
  if (!force) {
    if (params_.ignore_synthesis_text) {
      return;
    }
  }
  spotter_state_.synthesis_text = synthesis_text;
  auto lower_text = ToLowerRu(ToUtf16(synthesis_text));

  std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), [](char16_t ch) {
    return IsAlphaOrBlank(ch) ? ch : u' ';
  });

  auto clean_text = ToUtf8(lower_text);

  std::istringstream iss(clean_text);
  std::vector<std::string> words(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>());

  for (auto& class_meta : class_meta_infos_) {
    bool is_hotword_in_text = std::find(words.begin(), words.end(), class_meta.GetRuHotword()) != words.end();
    class_meta.SetTemporaryDisabled(is_hotword_in_text);
  }
}

void SpotterEventGenerator::ResetSynthesisText(bool force) {
  SetSynthesisText("", force);
}

} // namespace giga_edge
