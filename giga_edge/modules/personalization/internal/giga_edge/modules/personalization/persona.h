#pragma once

#include "giga_edge/lib/config_parser/config_parser.h"

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace giga_edge::persona {

inline constexpr auto kMale = "male";
inline constexpr auto kFemale = "female";

// The host voice in the model terms: the TTS voice and the gender of the character.
struct Voice {
  std::string tts_voice;
  std::string gender;
};

struct PersonaParams {
  // By the id of the host voice; an unknown id is treated as no voice chosen.
  std::map<std::string, Voice> voices;
};

inline constexpr auto kRoomPlaceholder = "{room}";

struct SystemPromptParams {
  std::string default_prompt;
  // By the key "gender/pronoun", for example "female/informal".
  std::map<std::string, std::string> prompts;
  // The speaker's room line after the character prompt, for example `You are in "{room}".`; empty means no room.
  std::string room_template;
  bool local_debug_enabled = false;
};

std::optional<Voice> FindVoice(const PersonaParams& params, const std::string& voice);

std::string PromptKey(const std::string& gender, const std::string& pronoun);

// Without a gender it is default_prompt; the second element tells whether a prompt was found for the pair.
std::pair<std::string, bool> SelectPrompt(
  const SystemPromptParams& params, const std::string& gender, const std::string& pronoun
);

std::string WithRoom(const SystemPromptParams& params, const std::string& prompt, const std::string& room);

} // namespace giga_edge::persona

namespace giga_edge {

template<>
persona::PersonaParams ParseConfig(const YAML::Node& config);

template<>
persona::SystemPromptParams ParseConfig(const YAML::Node& config);

} // namespace giga_edge
