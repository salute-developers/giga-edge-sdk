#pragma once

#include "params.h"

#include "giga_edge/modules/spotter/types.h"

namespace giga_edge {

class ClassMetaInfo : public IClassMetaInfo {
public:
  ClassMetaInfo(ClassMetaInfoParams meta_info_params, const ActivationMode& activation_mode);

  ~ClassMetaInfo() override = default;

  float GetThreshold() const override {
    return params_.threshold;
  };

  float GetSubThreshold() const override {
    return params_.sub_threshold;
  }

  float GetOvThreshold() const override {
    return params_.ov_threshold;
  }

  std::unordered_map<std::string, std::string> GetAsrVariables() const override {
    return params_.asr_variables;
  }

  std::string GetHotword() const override {
    return params_.hotword;
  }

  std::string GetRuHotword() const override {
    return params_.ru_hotword;
  }

  HotwordGroup GetHotwordGroup() const override {
    return params_.hotword_group;
  }

  int GetHotwordLengthMs() const override {
    return params_.hotword_length_ms;
  }

  bool IsEnabled() const {
    return enabled_ && !temporary_disabled_;
  }

  void SetTemporaryDisabled(bool temporary_disabled) {
    temporary_disabled_ = temporary_disabled;
  }

  void SetEnabled(bool enabled) {
    enabled_ = enabled;
  }

private:
  const ClassMetaInfoParams params_;
  bool enabled_;
  bool temporary_disabled_;
};

} // namespace giga_edge