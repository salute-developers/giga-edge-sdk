#include "giga_edge/modules/tts/interpreter/backends/vits/params.h"

#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include <catch.hpp>
#include <yaml-cpp/yaml.h>

#include <string>

using namespace giga_edge;
using Catch::Matchers::Contains;

namespace {

VitsTtsInterpreterParams Parse(const std::string& more = "") {
  return ParseConfig<VitsTtsInterpreterParams>(
    YAML::Load("{models_path: models, text_path: text/ru, voice: one, voices: {one: 3, two: 5}" + more + "}")
  );
}

} // namespace

TEST_CASE("vits params: the required keys, the defaults and every key", "[tts][vits]") {
  for (const std::string key : {"models_path", "text_path", "voice", "voices"}) {
    auto config = YAML::Load("{models_path: models, text_path: text/ru, voice: one, voices: {one: 3}}");
    config.remove(key);
    REQUIRE_THROWS_WITH(ParseConfig<VitsTtsInterpreterParams>(config), Contains("'" + key + "'"));
  }
  REQUIRE_NOTHROW(
    TtsBackendRegistry::ParseParams("vits", YAML::Load("{models_path: m, text_path: t, voice: a, voices: {a: 0}}"))
  );

  const auto defaults = Parse();
  REQUIRE(defaults.acoustic.models_path == "models");
  REQUIRE(defaults.text_path == "text/ru");
  REQUIRE(defaults.voice == "one");
  REQUIRE(defaults.voices.at("two") == 5);
  REQUIRE(defaults.max_text_chars == 100'000);
  REQUIRE_FALSE(defaults.verify_dictionaries);
  REQUIRE(defaults.sentences.max_sentence_chars == 180);
  REQUIRE(defaults.sentences.sentence_pause_ms == 500);
  REQUIRE(defaults.sentences.clause_pause_ms == 150);
  REQUIRE(defaults.sentences.word_pause_ms == 20);
  REQUIRE_FALSE(defaults.acoustic.mmap_models);
  REQUIRE(defaults.stress.homograph_strong_confidence == 0.9);
  REQUIRE(defaults.stress.homograph_mild_confidence == 0.9);

  const auto all = Parse(
    ", max_text_chars: 7, verify_dictionaries: true, max_sentence_chars: 40, sentence_pause_ms: 1, clause_pause_ms: 2,"
    " word_pause_ms: 3, speed: 2, threads: 4, onnx_environment: shared, optimization_level: disable,"
    " noise_scale: 0.1, noise_scale_w: 0.2, gain_db: -6, local_debug_enabled: true, mmap_models: true,"
    " homograph_strong_confidence: 0.95, homograph_mild_confidence: 0.6"
  );
  REQUIRE(all.max_text_chars == 7);
  REQUIRE(all.verify_dictionaries);
  REQUIRE(all.sentences.max_sentence_chars == 40);
  REQUIRE(all.sentences.sentence_pause_ms == 1);
  REQUIRE(all.sentences.clause_pause_ms == 2);
  REQUIRE(all.sentences.word_pause_ms == 3);
  REQUIRE(all.acoustic.speed == 2.0F);
  REQUIRE(all.acoustic.threads == 4);
  REQUIRE(all.acoustic.onnx_environment == "shared");
  REQUIRE(all.acoustic.optimization_level == OnnxOptimizationLevel::DISABLE);
  REQUIRE(all.acoustic.noise_scale == 0.1F);
  REQUIRE(all.acoustic.noise_scale_w == 0.2F);
  REQUIRE(all.acoustic.gain_db == -6.0F);
  REQUIRE(all.acoustic.local_debug_enabled);
  REQUIRE(all.acoustic.mmap_models);
  REQUIRE(all.stress.homograph_strong_confidence == 0.95);
  REQUIRE(all.stress.homograph_mild_confidence == 0.6);
}

TEST_CASE("vits params: values out of range are refused", "[tts][vits]") {
  REQUIRE_THROWS_WITH(
    ParseConfig<VitsTtsInterpreterParams>(YAML::Load("{models_path: m, text_path: t, voice: no, voices: {one: 3}}")),
    Contains("no voice 'no'")
  );
  REQUIRE_THROWS_WITH(
    ParseConfig<VitsTtsInterpreterParams>(
      YAML::Load("{models_path: m, text_path: t, voice: a, voices: {a: 0, b: -1}}")
    ),
    Contains("the speaker of voice 'b' is negative")
  );
  REQUIRE_THROWS_WITH(Parse(", max_text_chars: 0"), Contains("max_text_chars"));
  REQUIRE_THROWS_WITH(Parse(", max_text_chars: 1000001"), Contains("max_text_chars"));
  REQUIRE_NOTHROW(Parse(", max_text_chars: 1"));
  REQUIRE_NOTHROW(Parse(", max_text_chars: 1000000"));
  REQUIRE_THROWS(Parse(", max_sentence_chars: 0"));
  REQUIRE_NOTHROW(Parse(", max_sentence_chars: 400"));
  REQUIRE_THROWS(Parse(", max_sentence_chars: 401"));
  // A value of another type and an unknown key are errors, not defaults.
  REQUIRE_THROWS(Parse(", max_sentence_chars: many"));
  REQUIRE_THROWS(Parse(", verify_dictionaries: 12"));
  REQUIRE_THROWS(
    ParseConfig<VitsTtsInterpreterParams>(YAML::Load("{models_path: m, text_path: t, voice: one, voices: one}"))
  );
  REQUIRE_THROWS_WITH(Parse(", speeed: 2"), Contains("'speeed'"));
  REQUIRE_THROWS_WITH(Parse(", acoustic: {speed: 2}"), Contains("'acoustic'"));
  REQUIRE_THROWS(Parse(", sentence_pause_ms: -1"));
  REQUIRE_THROWS(Parse(", speed: 5"));
  for (const std::string key : {"homograph_strong_confidence", "homograph_mild_confidence"}) {
    REQUIRE_NOTHROW(Parse(", " + key + ": 0"));
    REQUIRE_NOTHROW(Parse(", " + key + ": 1"));
    for (const std::string value : {"-0.01", "1.01", ".nan"}) {
      REQUIRE_THROWS_WITH(Parse(", " + key + ": " + value), Contains("'" + key + "' must be in [0, 1]"));
    }
    REQUIRE_THROWS(Parse(", " + key + ": sure"));
  }
}
