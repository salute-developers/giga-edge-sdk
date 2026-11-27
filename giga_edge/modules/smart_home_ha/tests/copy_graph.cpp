#include "test_config.h"

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/nodes.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_connection.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/wait_utils.h"
#include "giga_edge/modules/smart_home_ha/nodes.h"

#include <catch.hpp>

#include <condition_variable>
#include <mutex>
#include <string_view>

using namespace giga_edge;
using namespace giga_edge::ha_test;

namespace {
constexpr std::string_view kGraph = R"(
num_threads: 1
version: ha_copy_test
lib_version: "1.31"
nodes:
  - type: mcp_completion_pickup
    name: pickup
    inputs: {user_query: query, dialogue_reset: reset, submission: report}
    outputs: {mcp_completions: completions}
    params: {environment_name: mcp, consumer_name: executor, owns_consumer_registration: false}
  - type: ha_tool_result
    name: result
    inputs: {mcp_completions: completions}
    outputs: {tool_results: results, continuations: continuations}
  - type: ha_action_resolver
    name: resolver
    inputs: {user_query: query, dialogue_reset: reset, assistant_result: generation, ha_catalog: catalog, continuations: continuations}
    outputs: {mcp_resolved_action: action}
  - type: mcp_action_executor
    name: executor
    inputs: {mcp_resolved_action: action}
    outputs: {mcp_action_result: report}
    params: {environment_name: mcp, consumer_name: executor}
)";

class CopyGraph {
public:
  CopyGraph()
    : env_(std::make_shared<mcp::McpServiceEnvironment>(mcp::McpServiceConfig{}))
    , connection_(std::make_shared<mcp::test_utils::FakeConnection>())
    , runtime_(
        CreateCachingDataReader(
          nullptr, {{"config.yaml", Create<IDataSpanPtr>(std::vector<char>(kGraph.begin(), kGraph.end()))}}
        ),
        "config.yaml",
        {{"mcp", env_}}
      ) {
    runtime_.Subscribe<CompactToolResults>({}, "results", [this](const auto& results, const std::any&) {
      std::lock_guard lock(mutex_);
      results_.insert(results_.end(), results.begin(), results.end());
      ++result_ticks_;
      ticked_.notify_all();
    });
    runtime_.Subscribe<McpActionSubmitReport>({}, "report", [this](const auto& report, const std::any&) {
      std::lock_guard lock(mutex_);
      if (report.submitted) {
        submitted_.push_back(report.action.tool_name);
      }
      ++report_ticks_;
      ticked_.notify_all();
    });
    connection_->EnqueueSuccess("initialize", ParseJsonOrThrow(R"({"protocolVersion":"2025-03-26",
      "serverInfo":{"name":"ha","version":"test"},"capabilities":{"tools":{}}})"));
    Json::Value tools;
    for (const auto& tool : CopySnapshot().tools) {
      Json::Value entry;
      entry["name"] = tool.tool_name;
      entry["inputSchema"]["type"] = "object";
      tools["tools"].append(entry);
    }
    connection_->EnqueueSuccess("tools/list", tools);
    env_->service->AddServer("ha", connection_);
    REQUIRE(mcp::test_utils::WaitUntil([&] {
      return !env_->service->GetToolsSnapshot().empty();
    }));
  }

  ~CopyGraph() {
    env_->service->RemoveServer("ha");
  }

  void ReplyWith(const Json::Value& value, const std::string& request_id = "") {
    Json::Value response;
    response["content"] = ParseJsonOrThrow(Reply({}, value).content_json);
    response["isError"] = false;
    connection_->EnqueueSuccess("tools/call", response, request_id);
  }

  mcp::test_utils::FakeConnection& Connection() {
    return *connection_;
  }

  void Tick(const AssistantResult& result = {}, const UserQuery& query = {}, bool reset = false) {
    size_t results_before, reports_before;
    {
      std::lock_guard lock(mutex_);
      results_before = result_ticks_;
      reports_before = report_ticks_;
    }
    runtime_.Push({}, "query", query, {});
    runtime_.Push({}, "reset", reset, {});
    runtime_.Push({}, "catalog", CopyCatalog(), {});
    runtime_.Push({}, "generation", result, {});
    std::unique_lock lock(mutex_);
    REQUIRE(ticked_.wait_for(lock, mcp::test_utils::kWaitHangGuard, [&] {
      return result_ticks_ > results_before && report_ticks_ > reports_before;
    }));
  }

  CompactToolResults Results() {
    std::lock_guard lock(mutex_);
    return results_;
  }

  // Tools of the submitted actions: unlike Calls(), complete as soon as Tick() returns.
  std::vector<std::string> Submitted() {
    std::lock_guard lock(mutex_);
    return submitted_;
  }

  std::vector<mcp::test_utils::FakeConnection::RequestCall> Calls() {
    std::vector<mcp::test_utils::FakeConnection::RequestCall> calls;
    for (const auto& call : connection_->RequestCalls()) {
      if (call.method == "tools/call") {
        calls.push_back(call);
      }
    }
    return calls;
  }

private:
  std::mutex mutex_;
  std::condition_variable ticked_;
  size_t result_ticks_ = 0, report_ticks_ = 0;
  CompactToolResults results_;
  std::vector<std::string> submitted_;
  std::shared_ptr<mcp::McpServiceEnvironment> env_;
  std::shared_ptr<mcp::test_utils::FakeConnection> connection_;
  Runtime runtime_;
};
} // namespace

TEST_CASE("HA copy completes once through the entire runtime graph", "[ha][copy][graph]") {
  CopyGraph graph;
  graph.ReplyWith(SourceContext());
  graph.ReplyWith(ReadContext());
  graph.ReplyWith(Success());
  graph.Tick(CopyAction(), {.request_id = "query-1", .text = "copy"});
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    graph.Tick();
    return !graph.Results().empty();
  }));
  for (int i = 0; i < 5; ++i) {
    graph.Tick();
  }
  const auto results = graph.Results();
  REQUIRE(results.size() == 1);
  CHECK_FALSE(results[0].is_error);
  CHECK(results[0].call_id == "query-1/tool/1");
  CHECK(graph.Submitted().size() == 3);
  const auto calls = graph.Calls();
  REQUIRE(calls.size() == 3);
  CHECK(calls[0].params["name"] == "homeassistant__GetLiveContext");
  CHECK(calls[0].params["arguments"]["name"] == "Desk lamp");
  CHECK(calls[1].params["name"] == "homeassistant__GetLiveContext");
  CHECK(calls[1].params["arguments"]["name"] == "Bedroom lamp");
  CHECK(calls[2].params["name"] == "light__HassLightSet");
  CHECK(calls[2].params["arguments"]["brightness"] == 73);
}

TEST_CASE("HA copy runtime reset after either read prevents the old write and result", "[ha][copy][graph]") {
  const size_t reads_before_reset = GENERATE(1, 2);
  CopyGraph graph;
  graph.ReplyWith(SourceContext());
  if (reads_before_reset == 2) {
    // Held until the loop below stops ticking: a tick that picked up the second read would send the write.
    graph.ReplyWith(ReadContext(), "second-read");
    graph.Connection().BlockRequest("second-read");
  }
  graph.Tick(CopyAction(), {.request_id = "query-1", .text = "copy"});
  if (reads_before_reset == 2) {
    REQUIRE(mcp::test_utils::WaitUntil([&] {
      graph.Tick();
      return graph.Calls().size() == 2;
    }));
    graph.Connection().UnblockRequest("second-read");
  }
  // No new graph tick: the server has finished the read, but its completion is not picked up yet.
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    const auto calls = graph.Calls();
    return calls.size() == reads_before_reset && calls.back().completed_at != std::chrono::steady_clock::time_point{};
  }));
  graph.Tick({}, {}, true);
  for (int i = 0; i < 5; ++i) {
    graph.Tick();
  }
  CHECK(graph.Results().empty());
  CHECK(graph.Submitted().size() == reads_before_reset);
  REQUIRE(graph.Calls().size() == reads_before_reset);

  graph.ReplyWith(SourceContext());
  graph.ReplyWith(ReadContext());
  graph.ReplyWith(Success());
  auto next = CopyAction();
  next.request_id = "query-2";
  next.tool_calls[0].id = "query-2/tool/1";
  graph.Tick(next, {.request_id = "query-2", .text = "copy again"});
  REQUIRE(mcp::test_utils::WaitUntil([&] {
    graph.Tick();
    return !graph.Results().empty();
  }));
  for (int i = 0; i < 5; ++i) {
    graph.Tick();
  }
  const auto results = graph.Results();
  REQUIRE(results.size() == 1);
  CHECK(results[0].call_id == "query-2/tool/1");
  CHECK_FALSE(results[0].is_error);
  const auto calls = graph.Calls();
  CHECK(graph.Submitted().size() == reads_before_reset + 3);
  REQUIRE(calls.size() == reads_before_reset + 3);
  CHECK(calls.back().params["name"] == "light__HassLightSet");
}
