#include "giga_edge/modules/personalization/persona.h"

#include "giga_edge/lib/common/str_utils.h"

#include <stdexcept>
#include <utility>

namespace giga_edge::persona {

std::optional<Voice> FindVoice(const PersonaParams& params, const std::string& voice) {
  const auto it = params.voices.find(voice);
  return it == params.voices.end() ? std::nullopt : std::optional<Voice>(it->second);
}

std::string PromptKey(const std::string& gender, const std::string& pronoun) {
  return gender + "/" + pronoun;
}

std::pair<std::string, bool> SelectPrompt(
  const SystemPromptParams& params, const std::string& gender, const std::string& pronoun
) {
  if (gender.empty()) {
    return {params.default_prompt, true};
  }
  const auto it = params.prompts.find(PromptKey(gender, pronoun));
  return it == params.prompts.end() ? std::make_pair(params.default_prompt, false) : std::make_pair(it->second, true);
}

std::string WithRoom(const SystemPromptParams& params, const std::string& prompt, const std::string& room) {
  if (params.room_template.empty() || room.empty()) {
    return prompt;
  }
  auto line = params.room_template;
  ReplaceAllInPlace(line, kRoomPlaceholder, room);
  return prompt.empty() ? line : prompt + "\n" + line;
}

} // namespace giga_edge::persona

namespace giga_edge {

template<>
persona::PersonaParams ParseConfig(const YAML::Node& config) {
  persona::PersonaParams result;
  for (const auto& item : config["voices"]) {
    const auto id = item.first.as<std::string>();
    persona::Voice voice{
      .tts_voice = item.second["tts_voice"].as<std::string>(""),
      .gender = item.second["gender"].as<std::string>(""),
    };
    if (voice.gender != persona::kMale && voice.gender != persona::kFemale) {
      throw std::invalid_argument(
        "persona: voice '" + id + "' needs gender male or female, got '" + voice.gender + "'"
      );
    }
    result.voices.emplace(id, std::move(voice));
  }
  return result;
}

template<>
persona::SystemPromptParams ParseConfig(const YAML::Node& config) {
  persona::SystemPromptParams result;
  result.default_prompt = config["default"].as<std::string>("");
  for (const auto& item : config["prompts"]) {
    result.prompts.emplace(item.first.as<std::string>(), item.second.as<std::string>());
  }
  result.room_template = config["room_template"].as<std::string>("");
  result.local_debug_enabled = config["local_debug_enabled"].as<bool>(result.local_debug_enabled);
  if (!result.room_template.empty() && result.room_template.find(persona::kRoomPlaceholder) == std::string::npos) {
    throw std::invalid_argument("persona_system_prompt: room_template needs {room}");
  }
  return result;
}

} // namespace giga_edge
