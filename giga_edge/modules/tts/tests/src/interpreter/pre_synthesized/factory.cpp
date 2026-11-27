#include "giga_edge/modules/tts/interpreter/interpreter.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("TTS Interpreter Factory", "[tts][interpreter][nodes][runtime]") {
  SECTION("Create PreSynthesizedTtsInterpreter") {
    auto interpreter = TtsBackendRegistry::CreateInterpreter("pre_synthesized");
    REQUIRE(interpreter != nullptr);
    REQUIRE(interpreter->IsReady() == false);
  }
}
