#include "giga_edge/modules/tts/interpreter/interpreter.h"
#include "giga_edge/modules/tts/nodes/tts_streaming_audio_output/params.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <functional>
#include <memory>
#include <string>

using namespace giga_edge;

namespace {

TtsBackend NoBackend() {
  return {
    [](const YAML::Node&) -> TtsInterpreterParams {
      return {};
    },
    []() -> std::unique_ptr<ITtsInterpreter> {
      return nullptr;
    },
  };
}

const bool kListed = TtsBackendRegistry::Register("registry_test_listed", NoBackend());

} // namespace

TEST_CASE("TTS backend registry rejects an unknown backend by name", "[tts][interpreter][runtime]") {
  REQUIRE(kListed);
  // The error names both the unknown name and the backends of the build.
  const auto require_unknown = [](const std::function<void()>& call) {
    REQUIRE_THROWS_WITH(
      call(), Catch::Contains("Unknown TTS backend 'no_such_backend'") && Catch::Contains("registry_test_listed")
    );
  };

  SECTION("node params") {
    require_unknown([] {
      ParseConfig<TtsNodeParams>(YAML::Load("{backend_type: no_such_backend, tts_interpreter_params: {}}"));
    });
  }
  SECTION("interpreter") {
    require_unknown([] {
      TtsBackendRegistry::CreateInterpreter("no_such_backend");
    });
  }
  SECTION("params") {
    require_unknown([] {
      TtsBackendRegistry::ParseParams("no_such_backend", YAML::Node());
    });
  }
}

TEST_CASE("TTS backend registry keeps the first backend of a name", "[tts][interpreter][runtime]") {
  REQUIRE(TtsBackendRegistry::Register("registry_test_twice", NoBackend()));
  REQUIRE_THROWS_WITH(
    TtsBackendRegistry::Register("registry_test_twice", NoBackend()),
    Catch::Contains("already registered: registry_test_twice")
  );
}

TEST_CASE("TTS node params require the backend name", "[tts][nodes][runtime]") {
  REQUIRE_THROWS_WITH(
    ParseConfig<TtsNodeParams>(YAML::Load("{tts_interpreter_params: {}}")), Catch::Contains("'backend_type'")
  );
}
