// The character on a whole graph: host environment -> persona -> character prompt -> LLM request, as in the core
// and function_calling.
#include "giga_edge/lib/common/scope_guard.h"
#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/personalization/capability.h"
#include "giga_edge/modules/personalization/capability_internal.h"
#include "giga_edge/modules/personalization/environment.h"
#include "giga_edge/modules/personalization/nodes.h"

#include <catch.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace giga_edge;

namespace {

constexpr auto kConfig = R"(
num_threads: 1
version: persona_scenario
lib_version: "1.26"

nodes:
  - type: persona
    name: persona
    inputs: {tick: tick}
    outputs: {tts_voice: persona_tts_voice, gender: persona_gender, pronoun: persona_pronoun}
    params:
      voices:
        voice_1: {tts_voice: tts_voice_1, gender: male}
        voice_2: {tts_voice: tts_voice_2, gender: male}
        voice_3: {tts_voice: tts_voice_3, gender: female}
        voice_4: {tts_voice: tts_voice_4, gender: female}
  - type: persona_system_prompt
    name: persona_system_prompt
    inputs: {gender: persona_gender, pronoun: persona_pronoun}
    outputs: {system_prompt: persona_system_prompt}
    params:
      default: ""
      prompts:
        male/informal: "Ты — мужской персонаж. Обращайся к пользователю на «ты»."
        male/formal: "Ты — мужской персонаж. Обращайся к пользователю на «вы»."
        female/informal: "Ты — женский персонаж. Обращайся к пользователю на «ты»."
        female/formal: "Ты — женский персонаж. Обращайся к пользователю на «вы»."
  - type: user_query_from_text
    name: user_query_from_text
    inputs: {text: text}
    outputs: {user_query: user_query}
  - type: dialogue_request_builder
    name: request
    inputs: {user_query: user_query, system_prompt: persona_system_prompt}
    outputs: {assistant_request: assistant_request}
    params: {system_prompt: "", warm_on_system_change: true}
)";

constexpr std::string_view kText = kConfig;

// The graph's replies to turns. Declared before the graph: the graph is destroyed first and removes the
// subscriptions.
class Replies {
public:
  void Subscribe(Runtime& model) {
    model.Subscribe<AssistantRequest>({}, "assistant_request", [this](const AssistantRequest& r, const std::any&) {
      std::lock_guard lock(mutex_);
      requests_.push_back(r);
    });
    model.Subscribe<std::string>({}, "persona_tts_voice", [this](const std::string& voice, const std::any&) {
      std::lock_guard lock(mutex_);
      voice_ = voice;
    });
  }

  // A user turn: the system prompt of the request and the TTS voice in the same tick.
  std::pair<std::string, std::string> Ask(Runtime& model, const std::string& text) {
    const auto request = Tick(model, text);
    REQUIRE(request.kind == AssistantRequestKind::GENERATE);
    REQUIRE(request.messages.front().role == MessageRole::SYSTEM);
    std::lock_guard lock(mutex_);
    return {request.messages.front().content, voice_};
  }

  AssistantRequest Tick(Runtime& model, const std::string& text = "") {
    const size_t before = Requests();
    model.Push(SubmodelVersionChain{}, "tick", false, {});
    model.Push(SubmodelVersionChain{}, "text", text, {});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while ((!model.Empty() || Requests() == before) && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(Requests() == before + 1);
    std::lock_guard lock(mutex_);
    return requests_.back();
  }

private:
  size_t Requests() {
    std::lock_guard lock(mutex_);
    return requests_.size();
  }

  std::mutex mutex_;
  std::vector<AssistantRequest> requests_;
  std::string voice_;
};

class Graph {
public:
  Graph()
    : environment_(std::make_shared<PersonalizationEnvironment>())
    , model_(
        CreateCachingDataReader(
          nullptr, {{"config.yaml", Create<IDataSpanPtr>(std::vector<char>(kText.begin(), kText.end()))}}
        ),
        "config.yaml",
        {{kPersonalizationEnvironmentName, environment_}}
      ) {
    replies_.Subscribe(model_);
  }

  PersonalizationEnvironment& Host() {
    return *environment_;
  }

  std::pair<std::string, std::string> Ask(const std::string& text) {
    return replies_.Ask(model_, text);
  }

  AssistantRequest Tick(const std::string& text = "") {
    return replies_.Tick(model_, text);
  }

private:
  std::shared_ptr<PersonalizationEnvironment> environment_;
  Replies replies_;
  Runtime model_;
};

// A model on disk, for the manager that reads the parts itself.
struct ModelDir {
  ModelDir() {
    // No mkdtemp on MSVC: create_directory is atomic and returns false on a taken name.
    std::random_device random;
    do {
      path = std::filesystem::temp_directory_path() / ("giga_edge_persona_" + std::to_string(random()));
    } while (!std::filesystem::create_directory(path));
    std::ofstream(path / "config.yaml") << kConfig;
  }

  ~ModelDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  ModelDir(const ModelDir&) = delete;
  ModelDir& operator=(const ModelDir&) = delete;

  RuntimePart Core() const {
    return RuntimePart{
      .id = "core", .data = ModelFileData{.path = path.string()}, .versions = {{"core", {.config_path = "config.yaml"}}}
    };
  }

  std::filesystem::path path;
};

EnvironmentsMap Provided(const PersonalizationEnvironmentPtr& environment) {
  EnvironmentsMap environments;
  CreatePersonalizationEnvironmentProvider(environment)->RegisterEnvironments(environments);
  return environments;
}

const std::string kMaleInformal = "Ты — мужской персонаж. Обращайся к пользователю на «ты».";
const std::string kMaleFormal = "Ты — мужской персонаж. Обращайся к пользователю на «вы».";
const std::string kFemaleInformal = "Ты — женский персонаж. Обращайся к пользователю на «ты».";
const std::string kFemaleFormal = "Ты — женский персонаж. Обращайся к пользователю на «вы».";

} // namespace

TEST_CASE("Persona graph: the host character reaches the request and the synthesis", "[personalization][graph]") {
  Graph graph;

  SECTION("nothing chosen - no character") {
    REQUIRE(graph.Ask("привет") == std::make_pair(std::string(), std::string()));
  }
  SECTION("every character of the table") {
    graph.Host().SetPronoun(PersonalizationPronoun::INFORMAL);
    graph.Host().SetVoice("voice_1");
    REQUIRE(graph.Ask("привет") == std::make_pair(kMaleInformal, std::string("tts_voice_1")));
    graph.Host().SetVoice("voice_2");
    REQUIRE(graph.Ask("привет") == std::make_pair(kMaleInformal, std::string("tts_voice_2")));
    graph.Host().SetVoice("voice_3");
    REQUIRE(graph.Ask("привет") == std::make_pair(kFemaleInformal, std::string("tts_voice_3")));
    graph.Host().SetVoice("voice_4");
    REQUIRE(graph.Ask("привет") == std::make_pair(kFemaleInformal, std::string("tts_voice_4")));
  }
  SECTION("the pronoun of the same character") {
    graph.Host().SetVoice("voice_4");
    REQUIRE(graph.Ask("привет").first == kFemaleFormal);
    graph.Host().SetPronoun(PersonalizationPronoun::INFORMAL);
    REQUIRE(graph.Ask("привет").first == kFemaleInformal);
    graph.Host().SetPronoun(PersonalizationPronoun::FORMAL);
    REQUIRE(graph.Ask("привет").first == kFemaleFormal);
  }
  SECTION("a voice out of the table, then none") {
    graph.Host().SetVoice("voice_2");
    REQUIRE(graph.Ask("привет") == std::make_pair(kMaleFormal, std::string("tts_voice_2")));
    graph.Host().SetVoice("eva");
    REQUIRE(graph.Ask("привет") == std::make_pair(std::string(), std::string()));
    graph.Host().SetVoice("voice_2");
    graph.Host().SetVoice("");
    REQUIRE(graph.Ask("привет") == std::make_pair(std::string(), std::string()));
  }
  SECTION("the pronoun alone gives no character") {
    graph.Host().SetPronoun(PersonalizationPronoun::INFORMAL);
    REQUIRE(graph.Ask("привет") == std::make_pair(std::string(), std::string()));
  }
  SECTION("a change between turns applies to the next turn") {
    graph.Host().SetVoice("voice_1");
    REQUIRE(graph.Ask("привет").first == kMaleFormal);
    graph.Tick();
    graph.Host().SetVoice("voice_3");
    graph.Tick();
    REQUIRE(graph.Ask("как дела").first == kFemaleFormal);
  }
}

TEST_CASE("Persona graph: the host updates the character while the graph runs", "[personalization][graph]") {
  Graph graph;
  // Two host threads change the voice and the pronoun; every turn is the character of one snapshot: the prompt by
  // the gender of the same voice.
  const std::set<std::pair<std::string, std::string>> consistent{
    {"", ""},
    {kMaleFormal, "tts_voice_1"},
    {kMaleInformal, "tts_voice_1"},
    {kFemaleFormal, "tts_voice_3"},
    {kFemaleInformal, "tts_voice_3"},
  };
  {
    std::atomic<bool> stop{false};
    std::thread voices([&] {
      for (size_t i = 0; !stop; ++i) {
        graph.Host().SetVoice(i % 2 == 0 ? "voice_1" : "voice_3");
      }
    });
    std::thread pronouns([&] {
      for (size_t i = 0; !stop; ++i) {
        graph.Host().SetPronoun(i % 2 == 0 ? PersonalizationPronoun::INFORMAL : PersonalizationPronoun::FORMAL);
      }
    });
    // The host threads stop on any exit, including a failed check in Ask.
    const ScopeGuard join([&] {
      stop = true;
      voices.join();
      pronouns.join();
    });
    for (int turn = 0; turn < 200; ++turn) {
      const auto persona = graph.Ask("привет");
      if (consistent.count(persona) == 0) {
        FAIL_CHECK("turn " << turn << ": " << persona.first << " / " << persona.second);
        break;
      }
    }
  }

  graph.Host().SetVoice("voice_4");
  graph.Host().SetPronoun(PersonalizationPronoun::INFORMAL);
  REQUIRE(graph.Ask("привет") == std::make_pair(kFemaleInformal, std::string("tts_voice_4")));
}

TEST_CASE("Persona graph: the capability drives the graph of its manager", "[personalization][graph][capability]") {
  const ModelDir dir;
  const auto environment = std::make_shared<PersonalizationEnvironment>();
  Replies replies;

  SECTION("one environment for the provider and the capability") {
    RuntimeManager manager(dir.Core(), {}, Provided(environment));
    manager.RegisterCapability<IPersonalizationCapability>(CreatePersonalizationCapability(manager, environment));
    auto& model = *manager.Model();
    replies.Subscribe(model);
    const auto persona = manager.GetCapability<IPersonalizationCapability>();
    REQUIRE(persona != nullptr);

    REQUIRE(replies.Ask(model, "привет") == std::make_pair(std::string(), std::string()));
    persona->SetVoice("voice_3");
    persona->SetPronoun(PersonalizationPronoun::INFORMAL);
    REQUIRE(replies.Ask(model, "привет") == std::make_pair(kFemaleInformal, std::string("tts_voice_3")));
    persona->SetVoice("voice_1");
    REQUIRE(replies.Ask(model, "привет") == std::make_pair(kMaleInformal, std::string("tts_voice_1")));

    // Neighboring capabilities see the same (they take the pronoun from here).
    const auto internal = std::dynamic_pointer_cast<IPersonalizationCapabilityInternal>(persona);
    REQUIRE(internal != nullptr);
    REQUIRE(internal->GetPersonalization().voice == "voice_1");
    REQUIRE(internal->GetPersonalization().pronoun == PersonalizationPronoun::INFORMAL);
    REQUIRE(environment->Get().personalization.voice == "voice_1");
  }
  SECTION("a capability without the environment keeps the values to itself") {
    RuntimeManager manager(dir.Core(), {}, Provided(environment));
    const auto persona = CreatePersonalizationCapability(manager);
    auto& model = *manager.Model();
    replies.Subscribe(model);
    persona->SetVoice("voice_3");
    REQUIRE(replies.Ask(model, "привет") == std::make_pair(std::string(), std::string()));
    REQUIRE(
      std::dynamic_pointer_cast<IPersonalizationCapabilityInternal>(persona)->GetPersonalization().voice == "voice_3"
    );
    REQUIRE(environment->Get().revision == 0);
  }
  SECTION("the environment outlives a recreated manager") {
    {
      RuntimeManager manager(dir.Core(), {}, Provided(environment));
      CreatePersonalizationCapability(manager, environment)->SetVoice("voice_4");
    }
    RuntimeManager manager(dir.Core(), {}, Provided(environment));
    auto& model = *manager.Model();
    replies.Subscribe(model);
    REQUIRE(replies.Ask(model, "привет") == std::make_pair(kFemaleFormal, std::string("tts_voice_4")));
  }
  SECTION("no provided environment - the default character") {
    RuntimeManager manager(dir.Core(), {}, {});
    auto& model = *manager.Model();
    replies.Subscribe(model);
    environment->SetVoice("voice_4");
    REQUIRE(replies.Ask(model, "привет") == std::make_pair(std::string(), std::string()));
  }
}
