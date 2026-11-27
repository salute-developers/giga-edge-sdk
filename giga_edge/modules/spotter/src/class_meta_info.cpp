#include "giga_edge/modules/spotter/class_meta_info.h"

namespace giga_edge {

ClassMetaInfo::ClassMetaInfo(ClassMetaInfoParams params, const ActivationMode& activation_mode)
  : params_(params)
  , enabled_(true)
  , temporary_disabled_(false) {
  if (activation_mode == ActivationMode::MAIN || activation_mode == ActivationMode::MAIN_AND_ACTIVE) {
    enabled_ = params_.hotword_group == HotwordGroup::MAIN;
  }
}

} // namespace giga_edge