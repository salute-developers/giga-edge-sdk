#pragma once

#include "giga_edge/modules/spotter/types.h"

#include <cstdint>
#include <vector>

namespace giga_edge {

struct ClassMetaInfoParams {
  std::string hotword;
  std::string ru_hotword;
  int hotword_length_ms;
  float sub_threshold;
  float threshold;
  HotwordGroup hotword_group;
  float ov_threshold = 0.0;
  std::unordered_map<std::string, std::string> asr_variables;
};

struct EventGeneratorParams {
  ActivationMode activation_mode;
  std::vector<ClassMetaInfoParams> class_meta_infos;
  std::string model_name;
  std::vector<std::string> channels;
  bool ignore_synthesis_text = false;
  size_t reset_synthesis_text_delay = 0;
};

struct SleepTimerParams {
  std::vector<int32_t> sleeping_timer;
};

} // namespace giga_edge