#include "giga_edge/modules/llm/detail/llm_local_debug.h"

#include <catch.hpp>

#include <iostream>
#include <optional>
#include <sstream>
#include <string>

using giga_edge::AssistantStatus;
using giga_edge::DraftSource;
using giga_edge::LlmLocalDebugSession;
using giga_edge::StreamFinishReason;

namespace {

class CapturedStdout {
public:
  CapturedStdout()
    : previous_(std::cout.rdbuf(buffer_.rdbuf())) {}
  ~CapturedStdout() {
    std::cout.rdbuf(previous_);
  }
  CapturedStdout(const CapturedStdout&) = delete;
  CapturedStdout& operator=(const CapturedStdout&) = delete;

  std::string Text() const {
    return buffer_.str();
  }

private:
  std::ostringstream buffer_;
  std::streambuf* previous_;
};

} // namespace

TEST_CASE("disabled llm debug session writes nothing", "[llm][debug]") {
  CapturedStdout captured;
  LlmLocalDebugSession session;
  REQUIRE_FALSE(session.Enabled());
  session.MarkGenerationStarted();
  session.LogGenerationFinished(StreamFinishReason::EOS, AssistantStatus::OK, 3, 0);
  session.LogGenerationStopTail(StreamFinishReason::EOS, StreamFinishReason::COMPLETED, 0, "secret");
  REQUIRE(captured.Text().empty());
}

TEST_CASE("enabled llm debug session reports timings without content", "[llm][debug]") {
  CapturedStdout captured;
  LlmLocalDebugSession session(true, false, DraftSource::MTP);
  REQUIRE(session.Enabled());
  session.MarkGenerationStarted();
  session.LogGenerationFinished(StreamFinishReason::MAX_TOKENS, AssistantStatus::OK, 42, 17);
  session.LogGenerationStopTail(StreamFinishReason::EOS, StreamFinishReason::COMPLETED, 2, "secret text");
  const auto text = captured.Text();
  REQUIRE(text.find("[llm_timing]") != std::string::npos);
  REQUIRE(text.find("generation_started") != std::string::npos);
  REQUIRE(text.find("generation_finished") != std::string::npos);
  REQUIRE(text.find("generated_tokens=42") != std::string::npos);
  REQUIRE(text.find(" mtp_accepted_drafts=17") != std::string::npos);
  REQUIRE(text.find("generation_stop_tail") != std::string::npos);
  REQUIRE(text.find("appended_stop_tokens=2") != std::string::npos);
  REQUIRE(text.find("secret") == std::string::npos);
}

TEST_CASE("llm debug session dumps the generated tail only with content enabled", "[llm][debug]") {
  CapturedStdout captured;
  LlmLocalDebugSession session(true, true);
  session.LogGenerationStopTail(StreamFinishReason::STOP_SEQUENCE, StreamFinishReason::STOP_SEQUENCE, 1, "line\nnext");
  REQUIRE(captured.Text().find("gen_tail_preview=\"line\\nnext\"") != std::string::npos);
}

TEST_CASE("llm debug session names accepted drafts by their source", "[llm][debug]") {
  const auto log = [](std::optional<DraftSource> source, size_t accepted) {
    CapturedStdout captured;
    const LlmLocalDebugSession session = source.has_value()
      ? LlmLocalDebugSession(true, false, *source)
      : LlmLocalDebugSession(true);
    session.LogGenerationFinished(StreamFinishReason::EOS, AssistantStatus::OK, 5, accepted);
    return captured.Text();
  };
  REQUIRE(log(DraftSource::MTP, 3).find(" mtp_accepted_drafts=3") != std::string::npos);
  REQUIRE(log(DraftSource::PROMPT_LOOKUP, 4).find(" lookup_accepted_drafts=4") != std::string::npos);
  REQUIRE(log(std::nullopt, 2).find(" lookup_accepted_drafts=2") != std::string::npos);
  REQUIRE(log(DraftSource::MTP, 0).find("accepted_drafts") == std::string::npos);
}
