#include "giga_edge/modules/personalization/persona.h"

#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/personalization/capability.h"
#include "giga_edge/modules/personalization/environment.h"
#include "giga_edge/modules/personalization/nodes.h"

#include <catch.hpp>

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace giga_edge;

namespace {

constexpr auto kVoices = R"(
voices:
  voice_1: {tts_voice: tts_voice_1, gender: male}
  voice_2: {tts_voice: tts_voice_2, gender: male}
  voice_3: {tts_voice: tts_voice_3, gender: female}
  voice_4: {tts_voice: tts_voice_4, gender: female}
)";

constexpr auto kPrompts = R"(
default: ""
prompts:
  male/formal: "Ты — мужской персонаж. Обращайся к пользователю на «вы»."
  male/informal: "Ты — мужской персонаж. Обращайся к пользователю на «ты»."
  female/formal: "Ты — женский персонаж. Обращайся к пользователю на «вы»."
  female/informal: "Ты — женский персонаж. Обращайся к пользователю на «ты»."
)";

using Persona = std::tuple<std::string, std::string, std::string>;

struct PersonaGraph {
  explicit PersonaGraph(std::shared_ptr<PersonalizationEnvironment> environment, const std::string& params = kVoices) {
    if (environment) {
      environments[kPersonalizationEnvironmentName] = std::move(environment);
    }
    node = CreateNode("persona", params, {}, {}, {}, {}, &environments);
  }

  Persona Tick() {
    NodePush(node, "tick", false);
    return {
      NodeGet<std::string>(node, "tts_voice"),
      NodeGet<std::string>(node, "gender"),
      NodeGet<std::string>(node, "pronoun")
    };
  }

  EnvironmentsMap environments;
  NodeContext node;
};

std::string Prompt(NodeContext& node, const std::string& gender, const std::string& pronoun) {
  NodePush(node, "gender", gender);
  NodePush(node, "pronoun", pronoun);
  return NodeGet<std::string>(node, "system_prompt");
}

} // namespace

TEST_CASE("persona: the host's voice gives the synthesis voice and the gender", "[personalization][persona]") {
  const auto environment = std::make_shared<PersonalizationEnvironment>();
  PersonaGraph graph(environment);
  REQUIRE(graph.Tick() == Persona{"", "", "formal"});

  for (const auto& [voice, expected] : std::vector<std::pair<std::string, Persona>>{
         {"voice_1", {"tts_voice_1", "male", "formal"}},
         {"voice_2", {"tts_voice_2", "male", "formal"}},
         {"voice_3", {"tts_voice_3", "female", "formal"}},
         {"voice_4", {"tts_voice_4", "female", "formal"}},
       }) {
    INFO(voice);
    environment->SetVoice(voice);
    REQUIRE(graph.Tick() == expected);
    REQUIRE(graph.Tick() == expected);
  }

  environment->SetPronoun(PersonalizationPronoun::INFORMAL);
  REQUIRE(graph.Tick() == Persona{"tts_voice_4", "female", "informal"});
  environment->SetPronoun(PersonalizationPronoun::FORMAL);
  REQUIRE(graph.Tick() == Persona{"tts_voice_4", "female", "formal"});
}

TEST_CASE("persona: no environment or no voice yet is the default persona", "[personalization][persona]") {
  PersonaGraph without(nullptr);
  REQUIRE(without.Tick() == Persona{"", "", "formal"});
  REQUIRE(without.Tick() == Persona{"", "", "formal"});

  const auto environment = std::make_shared<PersonalizationEnvironment>();
  environment->SetPronoun(PersonalizationPronoun::INFORMAL);
  PersonaGraph only_pronoun(environment);
  REQUIRE(only_pronoun.Tick() == Persona{"", "", "informal"});
}

TEST_CASE("persona: an unknown or cleared voice falls back to the default", "[personalization][persona]") {
  const auto environment = std::make_shared<PersonalizationEnvironment>();
  PersonaGraph graph(environment);
  environment->SetVoice("eva");
  REQUIRE(graph.Tick() == Persona{"", "", "formal"});
  environment->SetVoice("voice_3");
  REQUIRE(graph.Tick() == Persona{"tts_voice_3", "female", "formal"});
  environment->SetVoice("Voice_3");
  REQUIRE(graph.Tick() == Persona{"", "", "formal"});
  environment->SetVoice("voice_3");
  environment->SetVoice("");
  REQUIRE(graph.Tick() == Persona{"", "", "formal"});
}

TEST_CASE("persona: a voice may keep the synthesis default", "[personalization][persona]") {
  const auto environment = std::make_shared<PersonalizationEnvironment>();
  PersonaGraph graph(environment, "voices:\n  robot: {gender: male}\n");
  environment->SetVoice("robot");
  REQUIRE(graph.Tick() == Persona{"", "male", "formal"});
}

TEST_CASE("persona: the voice table is checked at load", "[personalization][persona]") {
  REQUIRE_THROWS(CreateNode("persona", "voices:\n  voice_1: {tts_voice: tts_voice_1}\n"));
  REQUIRE_THROWS(CreateNode("persona", "voices:\n  voice_1: {tts_voice: tts_voice_1, gender: man}\n"));
  REQUIRE_NOTHROW(CreateNode("persona", "voices: {}\n"));
  REQUIRE_NOTHROW(CreateNode("persona", ""));
}

TEST_CASE("persona: the environment revision moves only on a change", "[personalization][persona]") {
  PersonalizationEnvironment environment;
  REQUIRE(environment.Get().revision == 0);
  environment.SetVoice("voice_1");
  environment.SetVoice("voice_1");
  REQUIRE(environment.Get().revision == 1);
  environment.SetPronoun(PersonalizationPronoun::FORMAL);
  REQUIRE(environment.Get().revision == 1);
  environment.SetPronoun(PersonalizationPronoun::INFORMAL);
  REQUIRE(environment.Get().revision == 2);
  REQUIRE(environment.Get().personalization.voice == "voice_1");
  REQUIRE(environment.Get().personalization.pronoun == PersonalizationPronoun::INFORMAL);
}

TEST_CASE("persona_system_prompt: the prompt of the gender and pronoun", "[personalization][persona_system_prompt]") {
  auto node = CreateNode("persona_system_prompt", kPrompts);
  REQUIRE(Prompt(node, "", "formal").empty());
  REQUIRE(Prompt(node, "", "informal").empty());
  REQUIRE(Prompt(node, "male", "formal") == "Ты — мужской персонаж. Обращайся к пользователю на «вы».");
  REQUIRE(Prompt(node, "male", "informal") == "Ты — мужской персонаж. Обращайся к пользователю на «ты».");
  REQUIRE(Prompt(node, "female", "formal") == "Ты — женский персонаж. Обращайся к пользователю на «вы».");
  REQUIRE(Prompt(node, "female", "informal") == "Ты — женский персонаж. Обращайся к пользователю на «ты».");
  REQUIRE(Prompt(node, "female", "informal") == "Ты — женский персонаж. Обращайся к пользователю на «ты».");
  REQUIRE(Prompt(node, "", "informal").empty());
}

TEST_CASE("persona_system_prompt: a missing pair is the default prompt", "[personalization][persona_system_prompt]") {
  auto node = CreateNode("persona_system_prompt", "default: base\nprompts:\n  male/formal: m\n");
  REQUIRE(Prompt(node, "male", "formal") == "m");
  REQUIRE(Prompt(node, "male", "informal") == "base");
  REQUIRE(Prompt(node, "male", "informal") == "base");
  REQUIRE(Prompt(node, "female", "formal") == "base");
  REQUIRE(Prompt(node, "", "formal") == "base");
  REQUIRE(Prompt(node, "male", "formal") == "m");

  auto empty = CreateNode("persona_system_prompt", "");
  REQUIRE(Prompt(empty, "male", "formal").empty());
}

TEST_CASE("persona_system_prompt: the speaker's room after the character", "[personalization][persona_system_prompt]") {
  const std::string room_params = std::string(kPrompts) + "room_template: \"Ты стоишь в «{room}».\"\n";
  auto node = CreateNode("persona_system_prompt", room_params, {}, {"room"});
  const auto prompt = [&](const std::string& gender, const std::string& room) {
    NodePush(node, "gender", gender);
    NodePush(node, "pronoun", std::string("informal"));
    NodePush(node, "room", room);
    return NodeGet<std::string>(node, "system_prompt");
  };
  REQUIRE(prompt("male", "Кухня") == "Ты — мужской персонаж. Обращайся к пользователю на «ты».\nТы стоишь в «Кухня».");
  REQUIRE(prompt("", "Кухня") == "Ты стоишь в «Кухня».");
  // A change of the room changes the system prompt and with it the warm-up (warm_on_system_change).
  REQUIRE(prompt("", "Гостиная") == "Ты стоишь в «Гостиная».");
  REQUIRE(
    prompt("female", "Гостиная") == "Ты — женский персонаж. Обращайся к пользователю на «ты».\nТы стоишь в «Гостиная»."
  );
  REQUIRE(prompt("female", "") == "Ты — женский персонаж. Обращайся к пользователю на «ты».");
  REQUIRE(prompt("", "").empty());
}

TEST_CASE(
  "persona_system_prompt: without the template the room changes nothing", "[personalization][persona_system_prompt]"
) {
  auto wired = CreateNode("persona_system_prompt", kPrompts, {}, {"room"});
  auto plain = CreateNode("persona_system_prompt", kPrompts);
  for (const auto& gender : {"", "male", "female"}) {
    NodePush(wired, "gender", std::string(gender));
    NodePush(wired, "pronoun", std::string("formal"));
    NodePush(wired, "room", std::string("Кухня"));
    REQUIRE(NodeGet<std::string>(wired, "system_prompt") == Prompt(plain, gender, "formal"));
  }

  // The template is set, the input is not connected: the same as without a room.
  auto unwired = CreateNode(
    "persona_system_prompt", std::string(kPrompts) + "room_template: \"Ты стоишь в «{room}».\"\n"
  );
  REQUIRE(Prompt(unwired, "male", "formal") == "Ты — мужской персонаж. Обращайся к пользователю на «вы».");
  REQUIRE(Prompt(unwired, "", "formal").empty());

  REQUIRE_THROWS(CreateNode("persona_system_prompt", "room_template: \"Ты стоишь в комнате.\"\n"));
}

TEST_CASE(
  "persona_system_prompt: every placeholder takes the room, a room name is never expanded again",
  "[personalization][persona_system_prompt]"
) {
  auto node = CreateNode("persona_system_prompt", "room_template: \"{room}: ты в «{room}».\"\n", {}, {"room"});
  const auto prompt = [&](const std::string& room) {
    NodePush(node, "gender", std::string());
    NodePush(node, "pronoun", std::string());
    NodePush(node, "room", room);
    return NodeGet<std::string>(node, "system_prompt");
  };
  REQUIRE(prompt("Кухня") == "Кухня: ты в «Кухня».");
  REQUIRE(prompt("{room}") == "{room}: ты в «{room}».");
  REQUIRE(prompt("Кухня") == "Кухня: ты в «Кухня».");
}

TEST_CASE("persona helpers: lookup, key and choice", "[personalization]") {
  const auto params = ParseConfig<persona::PersonaParams>(YAML::Load(kVoices));
  REQUIRE(params.voices.size() == 4);
  REQUIRE(persona::FindVoice(params, "voice_4")->tts_voice == "tts_voice_4");
  REQUIRE_FALSE(persona::FindVoice(params, "").has_value());
  REQUIRE(persona::PromptKey("female", "informal") == "female/informal");

  const auto prompts = ParseConfig<persona::SystemPromptParams>(YAML::Load(kPrompts));
  REQUIRE(prompts.prompts.size() == 4);
  REQUIRE(persona::SelectPrompt(prompts, "", "formal") == std::make_pair(std::string(), true));
  REQUIRE(persona::SelectPrompt(prompts, "robot", "formal") == std::make_pair(std::string(), false));
}

TEST_CASE("persona: an environment of another type under the name is not the host's", "[personalization][persona]") {
  struct OtherEnvironment : Environment {};
  EnvironmentsMap environments{{kPersonalizationEnvironmentName, std::make_shared<OtherEnvironment>()}};
  auto node = CreateNode("persona", kVoices, {}, {}, {}, {}, &environments);
  NodePush(node, "tick", false);
  REQUIRE(NodeGet<std::string>(node, "tts_voice").empty());
  REQUIRE(NodeGet<std::string>(node, "gender").empty());
  REQUIRE(NodeGet<std::string>(node, "pronoun") == "formal");
}

TEST_CASE("persona: the provider gives the graph the host's environment itself", "[personalization][persona]") {
  const auto environment = std::make_shared<PersonalizationEnvironment>();
  EnvironmentsMap environments;
  CreatePersonalizationEnvironmentProvider(environment)->RegisterEnvironments(environments);
  REQUIRE(environments.size() == 1);
  REQUIRE(environments.at(kPersonalizationEnvironmentName) == environment);

  EnvironmentsMap without;
  CreatePersonalizationEnvironmentProvider(nullptr)->RegisterEnvironments(without);
  REQUIRE(without.empty());
}
