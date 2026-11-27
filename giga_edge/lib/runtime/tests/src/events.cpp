#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/runtime.h"

#include <catch.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace giga_edge;
using namespace std::chrono_literals;

namespace {

class ResetTicksNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {
    ++resets_;
  }

  void ProcessOnce() override {
    (void)GetTrigger();
    PushResets(resets_);
  }

  REGISTER_INPUT(reset_ticks_node, trigger, Trigger, int32_t, "");
  REGISTER_OUTPUT(reset_ticks_node, resets, Resets, int32_t, "");

private:
  int32_t resets_ = 0;
};

REGISTER_NODE(reset_ticks_node, ResetTicksNode, "");

constexpr int kNodes = 4;
constexpr int32_t kResets = 3000;

struct ResetCounters {
  // Results must outlive the graph: graph destruction can still invoke subscriptions.
  std::array<std::atomic<int32_t>, kNodes> last{};
  Runtime model;

  ResetCounters()
    : model(CreateAnyPathSameSpanDataReader(Create<IDataSpanPtr>(Config()))) {
    for (int i = 0; i < kNodes; ++i) {
      model.Subscribe<int32_t>({}, "resets" + std::to_string(i), [this, i](const int32_t& resets, const std::any&) {
        last[i] = resets;
      });
    }
  }

  static std::vector<char> Config() {
    std::string config = "num_threads: 2\nversion: test\nlib_version: \"1.22\"\n\nnodes:\n";
    for (int i = 0; i < kNodes; ++i) {
      const auto n = std::to_string(i);
      config.append("  - {type: reset_ticks_node, name: node")
        .append(n)
        .append(", inputs: {trigger: trigger}, outputs: {resets: resets")
        .append(n)
        .append("}}\n");
    }
    return {config.begin(), config.end()};
  }

  bool WaitEmpty() const {
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (!model.Empty()) {
      if (std::chrono::steady_clock::now() > deadline) {
        return false;
      }
      std::this_thread::yield();
    }
    return true;
  }

  void RequireAllResets() const {
    REQUIRE(WaitEmpty());
    for (int i = 0; i < kNodes; ++i) {
      INFO("node " << i);
      REQUIRE(last[i] == kResets);
    }
  }
};

void Spin(std::chrono::microseconds pause) {
  const auto until = std::chrono::steady_clock::now() + pause;
  while (std::chrono::steady_clock::now() < until) {
  }
}

} // namespace

TEST_CASE("Runtime, an event right before a tick is never skipped by a busy node", "[runtime][events]") {
  ResetCounters graph;
  int32_t tick = 0;
  for (int32_t reset = 0; reset < kResets; ++reset) {
    REQUIRE(graph.WaitEmpty());
    graph.model.Push({}, "trigger", tick++, {});
    Spin(std::chrono::microseconds(reset * 7 % 40));
    graph.model.Reset();
    graph.model.Push({}, "trigger", tick++, {});
  }
  graph.RequireAllResets();
}

TEST_CASE("Runtime, events from another thread are never skipped while ticks go", "[runtime][events]") {
  ResetCounters graph;
  std::atomic<bool> done{false};
  std::thread resetter([&graph, &done] {
    for (int32_t reset = 0; reset < kResets; ++reset) {
      Spin(std::chrono::microseconds(reset * 7 % 40));
      graph.model.Reset();
    }
    done = true;
  });
  int32_t tick = 0;
  while (!done) {
    graph.model.Push({}, "trigger", tick++, {});
    Spin(std::chrono::microseconds(tick * 3 % 20));
  }
  resetter.join();
  graph.model.Push({}, "trigger", tick, {});
  graph.RequireAllResets();
}
