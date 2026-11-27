#pragma once

#include "class_meta_info.h"
#include "params.h"

#include "giga_edge/modules/spotter/spotter_event.h"

#include <functional>

namespace giga_edge {

class SpotterEventGenerator {
public:
  explicit SpotterEventGenerator(const EventGeneratorParams& params);

  std::vector<SpotterEvent> GenerateSpotterEvents(
    const std::vector<float>& predictions, std::function<Json::Value()> latencies_callback = []() {
      return Json::Value(Json::objectValue);
    }
  );

  std::vector<IClassMetaInfoPtr> GetClassMetaInfos() const;

  SpotterState GetState() const;

  const EventGeneratorParams& GetParams() const;

  void SetActiveName(const std::string& active_name);

  void SetActivationMode(ActivationMode activation_mode);

  void SetSynthesisText(const std::string& synthesis_text, bool force = false);

  void ResetSynthesisText(bool force = true);

protected:
  void ApplySpotterStateChanges();

protected:
  EventGeneratorParams params_;
  std::vector<ClassMetaInfo> class_meta_infos_;

  SpotterState spotter_state_;
};

} // namespace giga_edge