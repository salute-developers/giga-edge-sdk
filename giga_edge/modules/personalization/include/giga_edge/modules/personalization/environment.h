#pragma once

#include "giga_edge/lib/environment/environment_base.h"
#include "giga_edge/modules/personalization/capability_type.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace giga_edge {

inline constexpr auto kPersonalizationEnvironmentName = "personalization";

// Host personalization for graph nodes; revision grows with every change.
class PersonalizationEnvironment : public Environment {
public:
  struct Snapshot {
    Personalization personalization;
    uint64_t revision = 0;
  };

  void SetPronoun(PersonalizationPronoun pronoun) {
    std::lock_guard lock(mutex_);
    if (snapshot_.personalization.pronoun != pronoun) {
      snapshot_.personalization.pronoun = pronoun;
      ++snapshot_.revision;
    }
  }

  void SetVoice(const std::string& voice) {
    std::lock_guard lock(mutex_);
    if (snapshot_.personalization.voice != voice) {
      snapshot_.personalization.voice = voice;
      ++snapshot_.revision;
    }
  }

  Snapshot Get() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
  }

private:
  mutable std::mutex mutex_;
  Snapshot snapshot_;
};

using PersonalizationEnvironmentPtr = std::shared_ptr<PersonalizationEnvironment>;

} // namespace giga_edge
