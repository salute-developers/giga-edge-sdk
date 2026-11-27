#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/modules/assistant_core/nodes.h"
#include "giga_edge/modules/llm/nodes.h"

#include <catch.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

using namespace giga_edge;

namespace {

AssistantRequest Request(AssistantRequestKind kind) {
  AssistantRequest request;
  request.kind = kind;
  request.request_id = "r1";
  request.messages.push_back({MessageRole::USER, "Исходный текст:\nпривет мир"});
  return request;
}

void RequireNoUsage(const GenerationUsage& usage) {
  REQUIRE(usage.context_tokens == 0);
  REQUIRE(usage.reused_tokens == 0);
  REQUIRE(usage.prefilled_tokens == 0);
  REQUIRE(usage.generated_tokens == 0);
  REQUIRE(usage.prefill_ms == 0);
  REQUIRE(usage.generation_ms == 0);
  REQUIRE(usage.prompt_label.empty());
}

// A runner without a model in the dialogue chain: a turn with a trace, like the LLM's.
constexpr auto kTraceGraph = R"(
num_threads: 1
version: passthrough_trace
lib_version: "1.28"

nodes:
  - type: user_query_from_text
    name: user_query
    inputs: {text: text}
    outputs: {user_query: user_query}
    params: {request_id_prefix: "q-"}
  - type: dialogue_request_builder
    name: request
    inputs: {user_query: user_query, dialogue_history: dialogue_history}
    outputs: {assistant_request: assistant_request}
  - type: llm_passthrough
    name: llm
    inputs: {assistant_request: assistant_request, text: text}
    outputs: {stream_chunk: stream_chunk, llm_report: llm_report}
  - type: stream_to_assistant_result
    name: result
    inputs: {stream_chunk: stream_chunk}
    outputs: {assistant_result: raw_result}
  - type: dialogue_state_update
    name: history
    inputs: {previous_history: dialogue_history, user_query: user_query, assistant_result: raw_result}
    outputs: {dialogue_history: dialogue_history, turn_trace: turn_trace}
    params: {max_turns: 4}
  - type: assistant_result_relay
    name: relay
    inputs: {input: raw_result, trace: turn_trace}
    outputs: {assistant_result: assistant_result}
)";

class TraceGraph {
public:
  TraceGraph()
    : runtime_(CreateCachingDataReader(nullptr, {{"config.yaml", Config()}}), "config.yaml") {
    runtime_.Subscribe<AssistantResult>({}, "assistant_result", [this](const AssistantResult& result, const std::any&) {
      {
        std::lock_guard lock(mutex_);
        if (result.is_final) {
          finals_.push_back(result);
        }
      }
      ready_.notify_all();
    });
    runtime_.Subscribe<LlmReport>({}, "llm_report", [this](const LlmReport& report, const std::any&) {
      std::lock_guard lock(mutex_);
      if (report.is_final) {
        reports_.push_back(report);
      }
    });
  }

  static IDataSpanPtr Config() {
    const std::string_view text = kTraceGraph;
    return Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end()));
  }

  AssistantResult Ask(const std::string& text) {
    std::unique_lock lock(mutex_);
    const size_t before = finals_.size();
    lock.unlock();
    runtime_.Push(SubmodelVersionChain{}, "text", text, {});
    lock.lock();
    REQUIRE(ready_.wait_for(lock, std::chrono::seconds(60), [&] {
      return finals_.size() > before;
    }));
    return finals_.back();
  }

  std::vector<LlmReport> Reports() {
    std::lock_guard lock(mutex_);
    return reports_;
  }

private:
  std::mutex mutex_;
  std::condition_variable ready_;
  std::vector<AssistantResult> finals_;
  std::vector<LlmReport> reports_;
  Runtime runtime_;
};

} // namespace

TEST_CASE("llm_passthrough answers with the user text", "[llm][node]") {
  auto node = CreateNode("llm_passthrough", "", {}, {}, {}, {}, nullptr, {"llm_report"});

  NodePush(node, "assistant_request", Request(AssistantRequestKind::GENERATE));
  NodePush(node, "text", std::string{"привет мир"});
  const auto chunks = NodeGetBatch<AssistantChunk>(node, "stream_chunk");
  REQUIRE(chunks.size() == 2);
  REQUIRE(chunks[0].meta.request_id == "r1");
  REQUIRE(ChunkText(chunks[0]) != nullptr);
  REQUIRE(ChunkText(chunks[0])->text == "привет мир");
  REQUIRE(chunks[1].meta.is_final);
  REQUIRE(chunks[1].meta.finish_reason == StreamFinishReason::COMPLETED);

  RequireNoUsage(chunks[1].meta.usage);

  const auto reports = NodeGetBatch<LlmReport>(node, "llm_report");
  REQUIRE(reports.size() == 2);
  REQUIRE(reports[1].is_final);
  REQUIRE(reports[1].request_id == "r1");
  RequireNoUsage(reports[1].usage);
  REQUIRE(reports[1].prompt_probabilities.empty());
}

TEST_CASE("llm_passthrough finishes a failed request with its error", "[llm][node]") {
  auto node = CreateNode("llm_passthrough", "");

  auto request = Request(AssistantRequestKind::GENERATE);
  request.status = AssistantStatus::ERROR;
  request.error = "scene snapshot error";
  NodePush(node, "assistant_request", request);
  NodePush(node, "text", std::string{"привет мир"});
  const auto chunks = NodeGetBatch<AssistantChunk>(node, "stream_chunk");
  REQUIRE(chunks.size() == 1);
  REQUIRE(chunks[0].meta.is_final);
  REQUIRE(chunks[0].meta.status == AssistantStatus::ERROR);
  REQUIRE(chunks[0].meta.error == "scene snapshot error");
  REQUIRE(chunks[0].meta.request_id == "r1");
  RequireNoUsage(chunks[0].meta.usage);
}

TEST_CASE("llm_passthrough idles on a non-generate request", "[llm][node]") {
  auto node = CreateNode("llm_passthrough", "");

  NodePush(node, "assistant_request", Request(AssistantRequestKind::NONE));
  NodePush(node, "text", std::string{});
  const auto chunks = NodeGetBatch<AssistantChunk>(node, "stream_chunk");
  REQUIRE(chunks.size() == 1);
  REQUIRE_FALSE(chunks[0].meta.is_final);
  REQUIRE(ChunkText(chunks[0]) == nullptr);
}

TEST_CASE("llm_passthrough turns come with a turn trace without usage", "[llm][node][trace]") {
  TraceGraph graph;
  for (const std::string text : {"привет мир", "как дела"}) {
    CAPTURE(text);
    const auto result = graph.Ask(text);
    REQUIRE(result.status == AssistantStatus::OK);
    REQUIRE(result.finish_reason == StreamFinishReason::COMPLETED);
    REQUIRE(result.text == text);
    RequireNoUsage(result.usage);
    const auto& trace = result.trace;
    REQUIRE_FALSE(trace.request_id.empty());
    REQUIRE(trace.request_id == result.request_id);
    REQUIRE(trace.user == text);
    REQUIRE(trace.finished);
    REQUIRE(trace.status == AssistantStatus::OK);
    REQUIRE(trace.finish_reason == StreamFinishReason::COMPLETED);
    REQUIRE(trace.steps.size() == 1);
    REQUIRE(trace.steps[0].raw_text == text);
    REQUIRE_FALSE(trace.steps[0].has_call);
    RequireNoUsage(trace.steps[0].usage);
  }
  const auto reports = graph.Reports();
  REQUIRE(reports.size() == 2);
  for (const auto& report : reports) {
    RequireNoUsage(report.usage);
  }
}
