#include "giga_edge/lib/node/event.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_output.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/lib/sequence_id_generator/sequence_id_generator.h"
#include "giga_edge/lib/submodel_version/submodel_version.h"

#include <catch.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace giga_edge;

namespace {

class ResetLoopNode : public NodeBase {
public:
  using NodeBase::NodeBase;

  void Reset() override {
    ++resets_;
  }

  bool InLoop() const {
    return IsInLoop();
  }

  int32_t Iterations() const {
    return iterations_;
  }

  int32_t Interrupts() const {
    return interrupts_;
  }

  int32_t Resets() const {
    return resets_;
  }

  REGISTER_INPUT(reset_loop_node, input, Input, int32_t, "");
  REGISTER_BATCH_OUTPUT(reset_loop_node, chunk, Chunk, int32_t, "");

private:
  void ProcessOnce() override {
    PushChunk(0);
    StartLoop();
  }

  void ProcessLoopIterationImpl() override {
    ++iterations_;
    PushChunk(iterations_);
  }

  void ProcessLoopInterruptImpl() override {
    ++interrupts_;
    PushChunk(-1);
  }

  int32_t GetMaxConsecutiveIterations() const override {
    return 1;
  }

  int32_t iterations_ = 0;
  int32_t interrupts_ = 0;
  int32_t resets_ = 0;
};

REGISTER_NODE(reset_loop_node, ResetLoopNode, "");

NodeContext StartedLoopNode() {
  NodeContext ctx = CreateNode("reset_loop_node", "{}");
  NodePush(ctx, "input", 1, /*is_last=*/true, /*max_iterations=*/3);
  return ctx;
}

} // namespace

TEST_CASE("Reset event takes the node out of an endless loop", "[node][reset][loop]") {
  auto ctx = StartedLoopNode();
  auto& node = static_cast<ResetLoopNode&>(*ctx.node);
  REQUIRE(node.InLoop());

  NodePushEvent(ctx, Event{.event = ResetEvent{}}, /*max_iterations=*/5);

  REQUIRE(node.Resets() == 1);
  REQUIRE_FALSE(node.InLoop());
  REQUIRE(ctx.node->GetNextProcessingType() == NextProcessingType::NONE);
}

TEST_CASE("Reset event closes the batch the interrupted loop had opened", "[node][reset][loop]") {
  auto ctx = StartedLoopNode();
  auto& node = static_cast<ResetLoopNode&>(*ctx.node);

  NodePushEvent(ctx, Event{.event = ResetEvent{}}, /*max_iterations=*/5);

  REQUIRE(node.Interrupts() == 1);
  const auto batch = NodeGetBatch<int32_t>(ctx, "chunk");
  REQUIRE_FALSE(batch.empty());
  REQUIRE(batch.back() == -1);
}

TEST_CASE("Reset event on an idle node leaves it idle", "[node][reset][loop]") {
  NodeContext ctx = CreateNode("reset_loop_node", "{}");
  auto& node = static_cast<ResetLoopNode&>(*ctx.node);

  NodePushEvent(ctx, Event{.event = ResetEvent{}}, /*max_iterations=*/5);

  REQUIRE(node.Resets() == 1);
  REQUIRE(node.Interrupts() == 0);
  REQUIRE_FALSE(node.InLoop());
}

TEST_CASE("Loop survives events that are not a reset", "[node][reset][loop]") {
  auto ctx = StartedLoopNode();
  auto& node = static_cast<ResetLoopNode&>(*ctx.node);
  const auto iterations_before = node.Iterations();

  NodeProcess(ctx, /*max_iterations=*/3);

  REQUIRE(node.InLoop());
  REQUIRE(node.Iterations() > iterations_before);
  REQUIRE(node.Resets() == 0);
}

namespace {

NodeContext CreateVersionedLoopNode() {
  NodeContext ctx;
  ctx.inputs["input"] = std::make_shared<NodeOutput>("input");
  ctx.outputs["chunk"] = std::make_shared<NodeOutput>("chunk");
  ctx.outputs["chunk"]->Subscribe("test", []() {});
  ctx.events = std::make_shared<NodeOutput>("events");
  ctx.id_generator = std::make_unique<SequenceIdGenerator>(std::vector<std::string>{"input"});

  NodeBaseConfig config{
    .submodel_version_chain = SubmodelVersionChain{{SubmodelVersionId{"llm", "cloud"}}},
    .events = ctx.events,
    .enqueue_callback = [](NodePtr) {},
    .common_node_params = {},
    .next_seq_id = ctx.id_generator->NextSeqId(),
    .environments = nullptr,
    .base_id = "reset_loop_node",
    .inputs = ctx.inputs,
    .outputs = ctx.outputs,
    .priority = 0,
  };
  ctx.node = NodeRegistry::CreateNode("reset_loop_node", config, nullptr, YAML::Node());
  ctx.node->Start();
  return ctx;
}

void SwitchTo(NodeContext& ctx, const std::string& version) {
  NodePushEvent(
    ctx,
    Event{
      .event = SetActiveSubmodelEvent{SubmodelVersionChain{{SubmodelVersionId{"llm", version}}}},
    },
    /*max_iterations=*/5
  );
}

} // namespace

TEST_CASE("Submodel switch does not leave a loop behind", "[node][reset][loop]") {
  auto ctx = CreateVersionedLoopNode();
  auto& node = static_cast<ResetLoopNode&>(*ctx.node);

  SwitchTo(ctx, "cloud");
  NodePush(ctx, "input", 1, /*is_last=*/true, /*max_iterations=*/3);
  REQUIRE(node.InLoop());

  SwitchTo(ctx, "local");
  REQUIRE_FALSE(node.InLoop());

  SwitchTo(ctx, "cloud");
  REQUIRE_FALSE(node.InLoop());
  REQUIRE(ctx.node->GetNextProcessingType() == NextProcessingType::NONE);
}
