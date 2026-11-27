#include "giga_edge/modules/model/config_parser/common/interpreter.h"

#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/model/interpreter/params.h"

#include <catch.hpp>

#include <string>
#include <utility>

using namespace giga_edge;

TEST_CASE("IsInterpreterTypeSupported", "[config_parser][interpreter]") {
  SECTION("agrees with ParseIntepreterType") {
    for (const std::string type : {
           "coreml",
           "tflite",
           "onnx",
           "ncnn",
         }) {
      INFO(type);
      if (IsInterpreterTypeSupported(type)) {
        REQUIRE_NOTHROW(ParseIntepreterType(type));
      } else {
        REQUIRE_THROWS(ParseIntepreterType(type));
      }
    }
  }

  SECTION("build flags") {
#ifdef ONNXRUNTIME_ENABLED
    REQUIRE(IsInterpreterTypeSupported("onnx"));
#else
    REQUIRE_FALSE(IsInterpreterTypeSupported("onnx"));
#endif
#ifdef NCNN_ENABLED
    REQUIRE(IsInterpreterTypeSupported("ncnn"));
#else
    REQUIRE_FALSE(IsInterpreterTypeSupported("ncnn"));
#endif
  }

  SECTION("every type has its own name") {
    using Type = InterpreterParams::InterpreterType;
    const std::pair<std::string, Type> types[] = {
      {"coreml", Type::COREML},
      {"tflite", Type::TFLITE},
      {"onnx", Type::ONNX},
      {"ncnn", Type::NCNN},
    };
    for (const auto& [name, type] : types) {
      INFO(name);
      REQUIRE(InterpreterTypeName(type) == name);
      if (IsInterpreterTypeSupported(name)) {
        REQUIRE(ParseIntepreterType(name) == type);
      } else {
        REQUIRE_THROWS_WITH(ParseIntepreterType(name), Catch::Matchers::EndsWith("interpreter is not supported"));
      }
    }
  }

  SECTION("unknown type") {
    REQUIRE_FALSE(IsInterpreterTypeSupported("unknown"));
    REQUIRE_THROWS(ParseIntepreterType("unknown"));
  }
}

#ifdef TFLITE_ENABLED
TEST_CASE("InterpreterParams tflite.use_xnnpack", "[config_parser][interpreter][tflite]") {
  SECTION("off by default") {
    REQUIRE_FALSE(ParseConfig<InterpreterParams>(YAML::Load("type: tflite\n")).tflite_params.use_xnnpack);
    REQUIRE_FALSE(ParseConfig<InterpreterParams>(YAML::Load("type: tflite\ntflite: {}\n")).tflite_params.use_xnnpack);
  }

  SECTION("enabled") {
    REQUIRE(
      ParseConfig<InterpreterParams>(YAML::Load("type: tflite\ntflite:\n  use_xnnpack: true\n"))
        .tflite_params.use_xnnpack
    );
  }
}
#endif

#ifdef ONNXRUNTIME_ENABLED
TEST_CASE("InterpreterParams onnx.optimization_level", "[config_parser][interpreter][onnx]") {
  using Level = OnnxOptimizationLevel;
  const auto parse = [](const std::string& onnx) {
    return ParseConfig<InterpreterParams>(YAML::Load("type: onnx\n" + onnx)).onnx_params.optimization_level;
  };

  SECTION("all by default") {
    REQUIRE(parse("") == Level::ALL);
    REQUIRE(parse("onnx: {}\n") == Level::ALL);
  }

  SECTION("every level") {
    REQUIRE(parse("onnx: {optimization_level: disable}\n") == Level::DISABLE);
    REQUIRE(parse("onnx: {optimization_level: basic}\n") == Level::BASIC);
    REQUIRE(parse("onnx: {optimization_level: extended}\n") == Level::EXTENDED);
    REQUIRE(parse("onnx: {optimization_level: all}\n") == Level::ALL);
  }

  SECTION("unknown level") {
    REQUIRE_THROWS(parse("onnx: {optimization_level: fastest}\n"));
  }
}
#endif
