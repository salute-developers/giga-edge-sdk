#include "giga_edge/lib/node/event.h"
#include "giga_edge/lib/node/loop_scheduler.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/lib/sequence_id_generator/sequence_id_generator.h"

#include <catch.hpp>

#include <memory>
#include <optional>

using namespace giga_edge;

namespace {

class TestLoopNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {
    iterations_ = 0;
  }

  void AfterAnyEvent() override {
    if (!finish_after_ || iterations_ < *finish_after_) {
      StartLoop();
    }
  }

  bool InLoop() const {
    return IsInLoop();
  }

  int32_t Iterations() {
    return iterations_;
  }

  void FinishAfter(int32_t iterations) {
    finish_after_ = iterations_ + iterations;
  }

  REGISTER_INPUT(test_loop_node, input, Input, int32_t, "");

private:
  void ProcessOnce() override {
    if (!finish_after_ || iterations_ < *finish_after_) {
      StartLoop();
    }
  }

  void ProcessLoopIterationImpl() override {
    ++iterations_;
    if (finish_after_ && iterations_ >= *finish_after_) {
      FinishLoop();
    }
  }

  void ProcessLoopInterruptImpl() override {}

  int32_t GetMaxConsecutiveIterations() const override {
    return 1;
  }

  int32_t iterations_ = 0;
  std::optional<int32_t> finish_after_;
};

REGISTER_NODE(test_loop_node, TestLoopNode, "");

} // namespace

TEST_CASE("No interruptions", "[runtime][loop_replay]") {
  auto recorder = std::make_shared<LoopRecorder>();
  NodeContext ctx_recorder = CreateNode("test_loop_node", "{}");
  auto& node_recorder = static_cast<TestLoopNode&>(*ctx_recorder.node);
  node_recorder.SetLoopScheduler(recorder);

  node_recorder.FinishAfter(3);

  NodePush(ctx_recorder, "input", 0, true, 0);
  NodeProcess(ctx_recorder);

  REQUIRE(!node_recorder.InLoop());
  REQUIRE(node_recorder.Iterations() == 3);
  REQUIRE(recorder->Trace() == LoopTrace{{"test_loop_node", {{.seq_id = 0, .iters = 3}}}});

  auto player = std::make_shared<LoopPlayer>(recorder->Trace());
  NodeContext ctx_player = CreateNode("test_loop_node", "{}");
  auto& node_player = static_cast<TestLoopNode&>(*ctx_player.node);
  node_player.SetLoopScheduler(player);

  NodePush(ctx_player, "input", 0, true, 0);
  NodeProcess(ctx_player);
  REQUIRE(!node_player.InLoop());
  REQUIRE(node_player.Iterations() == 3);

  REQUIRE(player->Divergences().empty());
}

TEST_CASE("Inputs interruptions", "[runtime][loop_replay]") {
  auto recorder = std::make_shared<LoopRecorder>();
  NodeContext ctx_recorder = CreateNode("test_loop_node", "{}");
  auto& node_recorder = static_cast<TestLoopNode&>(*ctx_recorder.node);
  node_recorder.SetLoopScheduler(recorder);

  node_recorder.FinishAfter(15);

  NodePush(ctx_recorder, "input", 0, true, 1);
  NodeProcess(ctx_recorder, 5);
  REQUIRE(node_recorder.Iterations() == 5);
  REQUIRE(node_recorder.InLoop());

  NodePush(ctx_recorder, "input", 0, true, 2);
  NodeProcess(ctx_recorder, 3);
  REQUIRE(node_recorder.Iterations() == 8);
  REQUIRE(node_recorder.InLoop());

  NodePush(ctx_recorder, "input", 0, true, 2);
  NodeProcess(ctx_recorder);

  REQUIRE(node_recorder.Iterations() == 15);
  REQUIRE(!node_recorder.InLoop());
  REQUIRE(
    recorder->Trace()
    == LoopTrace{{"test_loop_node", {{.seq_id = 0, .iters = 5}, {.seq_id = 1, .iters = 3}, {.seq_id = 2, .iters = 7}}}}
  );

  auto player = std::make_shared<LoopPlayer>(recorder->Trace());
  NodeContext ctx_player = CreateNode("test_loop_node", "{}");
  auto& node_player = static_cast<TestLoopNode&>(*ctx_player.node);
  node_player.SetLoopScheduler(player);

  NodePush(ctx_player, "input", 0, true, 0);
  NodePush(ctx_player, "input", 0, true, 0);
  NodePush(ctx_player, "input", 0, true, 0);

  NodeProcess(ctx_player, 7); // inputs + 5 iterations + interrupt
  REQUIRE(node_player.Iterations() == 5);
  REQUIRE(!node_player.InLoop());

  NodeProcess(ctx_player, 5); // inputs + 3 iterations + interrupt
  REQUIRE(node_player.Iterations() == 8);
  REQUIRE(!node_player.InLoop());

  NodeProcess(ctx_player);
  REQUIRE(node_player.Iterations() == 15);
  REQUIRE(!node_player.InLoop());

  REQUIRE(player->Divergences().empty());
}

TEST_CASE("Events interruptions", "[runtime][loop_replay]") {
  auto recorder = std::make_shared<LoopRecorder>();
  NodeContext ctx_recorder = CreateNode("test_loop_node", "{}");
  auto& node_recorder = static_cast<TestLoopNode&>(*ctx_recorder.node);
  node_recorder.SetLoopScheduler(recorder);

  node_recorder.FinishAfter(15);

  NodePush(ctx_recorder, "input", 0, true, 1);
  NodeProcess(ctx_recorder, 5);
  REQUIRE(node_recorder.Iterations() == 5);
  REQUIRE(node_recorder.InLoop());

  NodePushEvent(ctx_recorder, {}, 2);
  NodeProcess(ctx_recorder, 3);
  REQUIRE(node_recorder.Iterations() == 8);
  REQUIRE(node_recorder.InLoop());

  NodePushEvent(ctx_recorder, {}, 2);
  NodeProcess(ctx_recorder);

  REQUIRE(node_recorder.Iterations() == 15);
  REQUIRE(!node_recorder.InLoop());
  REQUIRE(
    recorder->Trace()
    == LoopTrace{{"test_loop_node", {{.seq_id = 0, .iters = 5}, {.seq_id = 1, .iters = 3}, {.seq_id = 2, .iters = 7}}}}
  );

  auto player = std::make_shared<LoopPlayer>(recorder->Trace());
  NodeContext ctx_player = CreateNode("test_loop_node", "{}");
  auto& node_player = static_cast<TestLoopNode&>(*ctx_player.node);
  node_player.SetLoopScheduler(player);

  NodePush(ctx_player, "input", 0, true, 0);
  NodePushEvent(ctx_player, {}, 0);
  NodePushEvent(ctx_player, {}, 0);

  NodeProcess(ctx_player, 7); // inputs + 5 iterations + interrupt
  REQUIRE(node_player.Iterations() == 5);
  REQUIRE(!node_player.InLoop());

  NodeProcess(ctx_player, 5); // inputs + 3 iterations + interrupt
  REQUIRE(node_player.Iterations() == 8);
  REQUIRE(!node_player.InLoop());

  NodeProcess(ctx_player);
  REQUIRE(node_player.Iterations() == 15);
  REQUIRE(!node_player.InLoop());

  REQUIRE(player->Divergences().empty());
}