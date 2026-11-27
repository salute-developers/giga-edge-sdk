#include "giga_edge/modules/assistant_core/turn_trace.h"

#include <catch.hpp>

#include <chrono>
#include <string>

using namespace giga_edge;
using namespace std::chrono_literals;

namespace {

const std::chrono::steady_clock::time_point t0{};

AssistantResult Final(const std::string& text) {
  return AssistantResult{.request_id = "r1", .raw_text = text, .text = text, .is_final = true};
}

AssistantResult Call(const std::string& name, const std::string& arguments) {
  auto result = Final("");
  result.raw_text = "<call>";
  result.tool_calls.push_back(ToolCall{.name = name, .arguments_json = arguments});
  return result;
}

} // namespace

TEST_CASE("TurnTraceRecorder times rounds and tool calls of a turn", "[assistant][trace]") {
  TurnTraceRecorder trace;
  REQUIRE_FALSE(trace.IsActive());
  REQUIRE_FALSE(trace.ToolResult("ok", t0));

  trace.Queue(t0);
  trace.Start("r1", "включи свет", t0 + 20ms);
  REQUIRE(trace.IsActive());
  REQUIRE_FALSE(trace.ToolResult("ok", t0 + 21ms));

  auto call = Call("set_device_parameter", R"({"device": "lamp"})");
  call.usage = GenerationUsage{.context_tokens = 300, .prefilled_tokens = 100, .prefill_ms = 40.5};
  trace.Round(call, t0 + 120ms);
  auto snapshot = trace.Snapshot(t0 + 120ms);
  REQUIRE(snapshot.request_id == "r1");
  REQUIRE(snapshot.user == "включи свет");
  REQUIRE(snapshot.wait_ms == 20);
  REQUIRE(snapshot.ms == 120);
  REQUIRE_FALSE(snapshot.finished);
  REQUIRE(snapshot.steps.size() == 1);
  REQUIRE(snapshot.steps[0].ms == 100);
  REQUIRE(snapshot.steps[0].usage.context_tokens == 300);
  REQUIRE(snapshot.steps[0].usage.prefill_ms == 40.5);
  REQUIRE(snapshot.steps[0].raw_text == "<call>");
  REQUIRE(snapshot.steps[0].has_call);
  REQUIRE(snapshot.steps[0].call.name == "set_device_parameter");
  REQUIRE(snapshot.steps[0].call.arguments_json == R"({"device": "lamp"})");
  REQUIRE_FALSE(snapshot.steps[0].has_result);

  REQUIRE(trace.ToolResult(R"({"success": true})", t0 + 150500us));
  trace.Round(Final("Включил."), t0 + 400ms);
  const auto out = trace.Finish(Final("Включил."), t0 + 400ms);
  REQUIRE_FALSE(trace.IsActive());
  REQUIRE(out.finished);
  REQUIRE(out.status == AssistantStatus::OK);
  REQUIRE(out.finish_reason == StreamFinishReason::NONE);
  REQUIRE(out.ms == 400);
  REQUIRE(out.steps.size() == 2);
  REQUIRE(out.steps[0].has_result);
  REQUIRE(out.steps[0].result == R"({"success": true})");
  REQUIRE(out.steps[0].result_ms == 30.5);
  REQUIRE(out.steps[1].ms == 249.5);
  REQUIRE_FALSE(out.steps[1].has_call);
  REQUIRE(out.steps[1].raw_text == "Включил.");
}

TEST_CASE("TurnTraceRecorder keeps a failed round without its call and the error", "[assistant][trace]") {
  TurnTraceRecorder trace;
  trace.Queue(t0);
  trace.Start("r1", "свет", t0);
  auto failed = Call("play", "{}");
  failed.status = AssistantStatus::ERROR;
  failed.error = "Repeated tool call";
  trace.Round(failed, t0 + 1ms);
  REQUIRE_FALSE(trace.ToolResult("ok", t0 + 2ms));
  const auto out = trace.Finish(failed, t0 + 3ms);
  REQUIRE(out.steps.size() == 1);
  REQUIRE_FALSE(out.steps[0].has_call);
  REQUIRE(out.status == AssistantStatus::ERROR);
  REQUIRE(out.error == "Repeated tool call");
}

TEST_CASE("TurnTraceRecorder starts over with the next turn", "[assistant][trace]") {
  TurnTraceRecorder trace;
  trace.Queue(t0);
  trace.Start("r1", "свет", t0);
  trace.Round(Call("play", "{}"), t0 + 5ms);
  trace.Queue(t0 + 10ms);
  trace.Start("r2", "музыка", t0 + 10ms);
  const auto out = trace.Snapshot(t0 + 12ms);
  REQUIRE(out.request_id == "r2");
  REQUIRE(out.wait_ms == 0);
  REQUIRE(out.ms == 2);
  REQUIRE(out.steps.empty());
}

TEST_CASE("TurnTraceRecorder without a started turn gives nothing", "[assistant][trace]") {
  TurnTraceRecorder trace;
  trace.Round(Call("play", "{}"), t0);
  REQUIRE_FALSE(trace.IsActive());
  REQUIRE(trace.Snapshot(t0).request_id.empty());
  REQUIRE(trace.Finish(Final("Готово."), t0).request_id.empty());
  REQUIRE_FALSE(trace.IsActive());
}
