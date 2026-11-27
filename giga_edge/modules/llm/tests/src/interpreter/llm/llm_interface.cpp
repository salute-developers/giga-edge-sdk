#include "giga_edge/lib/data_reader/data_reader.h"
#include "giga_edge/modules/llm/interpreter/interpreter.h"
#include "giga_edge/modules/llm/interpreter/params.h"
#include "giga_edge/modules/llm/nodes/llm_generate/params.h"

#include <catch.hpp>

#include <cstdint>
#include <thread>

using namespace giga_edge;

TEST_CASE("LLM Parameters validation", "[llm]") {
  SECTION("Default parameters") {
    LlmInterpreterParams params;
    REQUIRE(params.seed == -1);
    REQUIRE(params.top_k == 40);
    REQUIRE(params.top_p == 0.95f);
    REQUIRE(params.temp == 0.8f);
    REQUIRE(params.n_ctx == 2048);
    REQUIRE(params.n_batch == 512);
    REQUIRE(params.n_threads == 0);
    REQUIRE(params.n_threads_batch == 0);
    REQUIRE(params.flash_attn == "auto");
  }

  SECTION("Threads auto follow the performance cores") {
    const auto params = ParseLlmInterpreterParams(
      YAML::Load("{model_path: m.gguf, n_threads: auto, n_threads_batch: 3}")
    );
    REQUIRE(params.n_threads > 0);
    REQUIRE(params.n_threads <= static_cast<int32_t>(std::thread::hardware_concurrency()));
    REQUIRE(params.n_threads_batch == 3);
  }

  SECTION("Custom parameters") {
    LlmInterpreterParams params{
      .model_path = "/path/to/model.gguf",
      .stop_sequence = "</s>",
      .seed = 42,
      .top_k = 50,
      .top_p = 0.9f,
      .temp = 0.7f,
      .n_ctx = 1024,
      .n_batch = 256,
      .n_threads = 4,
      .n_threads_batch = 3,
      .flash_attn = "enabled"
    };

    REQUIRE(params.model_path == "/path/to/model.gguf");
    REQUIRE(params.stop_sequence == "</s>");
    REQUIRE(params.seed == 42);
    REQUIRE(params.top_k == 50);
    REQUIRE(params.top_p == 0.9f);
    REQUIRE(params.temp == 0.7f);
    REQUIRE(params.n_ctx == 1024);
    REQUIRE(params.n_batch == 256);
    REQUIRE(params.n_threads == 4);
    REQUIRE(params.n_threads_batch == 3);
    REQUIRE(params.flash_attn == "enabled");
  }
}

TEST_CASE("LLM Interpreter Factory", "[llm][llama.cpp]") {
  SECTION("Interpreter is not ready by default") {
    auto interpreter = LlmInterpreterFactory::CreateInterpreter(LlmInterpreterFactory::BackendType::LLAMA_CPP);
    REQUIRE(interpreter != nullptr);
    REQUIRE(interpreter->IsReady() == false);
  }
}

TEST_CASE("LLM Interpreter interface", "[llm][llama.cpp]") {
  auto interpreter = LlmInterpreterFactory::CreateInterpreter(LlmInterpreterFactory::BackendType::LLAMA_CPP);
  REQUIRE(interpreter != nullptr);

  SECTION("Interface methods exist and are callable") {
    REQUIRE_NOTHROW(interpreter->ClearContext());
    REQUIRE_FALSE(interpreter->TokenizePrompt("prompt").has_value());
    REQUIRE_FALSE(interpreter->AppendPromptTokens({1, 2, 3}));
    REQUIRE_FALSE(interpreter->TruncateContextTo(0));
    REQUIRE_FALSE(interpreter->GetContextTokenCount().has_value());
    REQUIRE_NOTHROW(interpreter->StopGeneration());
  }

  SECTION("IsReady returns false when not initialized") {
    REQUIRE(interpreter->IsReady() == false);
  }
}

TEST_CASE("LLM Interpreter initialization failure", "[llm][llama.cpp]") {
  auto interpreter = LlmInterpreterFactory::CreateInterpreter(LlmInterpreterFactory::BackendType::LLAMA_CPP);
  REQUIRE(interpreter != nullptr);

  SECTION("Initialize with invalid model path") {
    LlmInterpreterParams params{.model_path = "/nonexistent/model.gguf", .n_ctx = 512, .n_batch = 32};

    auto data_reader = CreateDataReader(".");
    bool result = interpreter->Initialize(params, data_reader);
    REQUIRE(result == false);
    REQUIRE(interpreter->IsReady() == false);
  }

  SECTION("Initialize with empty model path") {
    LlmInterpreterParams params{.model_path = "", .n_ctx = 512, .n_batch = 32};

    auto data_reader = CreateDataReader(".");
    bool result = interpreter->Initialize(params, data_reader);
    REQUIRE(result == false);
    REQUIRE(interpreter->IsReady() == false);
  }
}
