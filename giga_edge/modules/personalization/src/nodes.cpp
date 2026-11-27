#include "giga_edge/modules/personalization/nodes.h"

#include "giga_edge/lib/enum/enum.h"

#include <iostream>
#include <utility>

namespace giga_edge {

PersonaNode::PersonaNode(const NodeBaseConfig& base_config, const persona::PersonaParams& params)
  : NodeBase(base_config)
  , params_(params) {
  if (base_config.environments) {
    const auto it = base_config.environments->find(kPersonalizationEnvironmentName);
    if (it != base_config.environments->end()) {
      environment_ = std::dynamic_pointer_cast<PersonalizationEnvironment>(it->second);
    }
  }
}

void PersonaNode::ProcessOnce() {
  const auto snapshot = environment_ ? environment_->Get() : PersonalizationEnvironment::Snapshot{};
  if (revision_ != snapshot.revision) {
    revision_ = snapshot.revision;
    Refresh(snapshot.personalization);
  }
  PushTtsVoice(tts_voice_);
  PushGender(gender_);
  PushPronoun(pronoun_);
}

void PersonaNode::Refresh(const Personalization& personalization) {
  const auto voice = persona::FindVoice(params_, personalization.voice);
  if (!voice && !personalization.voice.empty()) {
    std::cerr << "[WARNING] persona: unknown voice '" << personalization.voice
              << "', the synthesis default and no character gender" << std::endl;
  }
  tts_voice_ = voice ? voice->tts_voice : "";
  gender_ = voice ? voice->gender : "";
  pronoun_ = EnumToString(personalization.pronoun);
  std::cerr << "[persona] voice '" << personalization.voice << "': tts_voice '" << tts_voice_ << "', gender '"
            << gender_ << "', pronoun '" << pronoun_ << "'" << std::endl;
}

void PersonaSystemPromptNode::ProcessOnce() {
  auto key = std::make_pair(GetGender(), GetPronoun());
  const auto room = GetRoom();
  const auto& room_name = room.has_value() ? room->get() : room_;
  if (key_ != key || room_ != room_name) {
    const auto [prompt, found] = persona::SelectPrompt(params_, key.first, key.second);
    if (!found && warned_.insert(persona::PromptKey(key.first, key.second)).second) {
      std::cerr << "[WARNING] persona_system_prompt: no prompt for '" << persona::PromptKey(key.first, key.second)
                << "', the default one" << std::endl;
    }
    if (params_.local_debug_enabled && room_ != room_name) {
      std::cerr << "[persona_system_prompt] room '" << room_name << "'" << std::endl;
    }
    prompt_ = persona::WithRoom(params_, prompt, room_name);
    key_ = std::move(key);
    room_ = room_name;
  }
  PushSystemPrompt(prompt_);
}

} // namespace giga_edge
