#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/features/nodes/feature_iterator_with_reset/node.h"

#include <catch.hpp>

#include <string>

using namespace giga_edge;

namespace {
NodeContext Iterator(const std::string& extra_params = "") {
  return CreateNode(
    "feature_iterator_with_reset",
    "step_size: 2\ncontext_width: 2\nnum_mels: 1\n" + extra_params,
    {},
    {"need_reset", "push_features"},
    {},
    {},
    nullptr,
    {"push_features_batch"}
  );
}

void Push(NodeContext& node, std::vector<float> features, bool reset, bool flush = false) {
  NodePush(node, "need_reset", reset);
  NodePush(node, "push_features", flush);
  NodePush(node, "features", features);
}

struct Batches {
  std::vector<std::vector<float>> features;
  std::vector<bool> resets;
  std::vector<bool> tails;
};

Batches Get(NodeContext& node) {
  return {
    NodeGetBatch<std::vector<float>>(node, "features_batch"),
    NodeGetBatch<bool>(node, "need_reset_batch"),
    NodeGetBatch<bool>(node, "push_features_batch"),
  };
}
} // namespace

TEST_CASE("Feature iterator resets once per utterance, not once per batch lane", "[features][nodes]") {
  auto node = Iterator();
  Push(node, {1, 2, 3, 4, 5}, true, true);
  REQUIRE(
    NodeGetBatch<std::vector<float>>(node, "features_batch") == std::vector<std::vector<float>>{{1, 2}, {3, 4}, {5}}
  );
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>{true, false, false});
}

TEST_CASE("Feature iterator retains reset until the first nonempty batch", "[features][nodes]") {
  auto node = Iterator();
  Push(node, {}, true);
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "features_batch") == std::vector<std::vector<float>>{{}});
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>{true});
  Push(node, {1}, false);
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "features_batch") == std::vector<std::vector<float>>{{}});
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>{true});
  Push(node, {2, 3, 4, 5}, false, true);
  REQUIRE(
    NodeGetBatch<std::vector<float>>(node, "features_batch") == std::vector<std::vector<float>>{{1, 2}, {3, 4}, {5}}
  );
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>{true, false, false});
}

TEST_CASE("Feature iterator drops an unfinished previous utterance on reset", "[features][nodes]") {
  auto node = Iterator();
  Push(node, {99}, true);
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "features_batch") == std::vector<std::vector<float>>{{}});
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>{true});
  Push(node, {1, 2}, true);
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "features_batch") == std::vector<std::vector<float>>{{1, 2}});
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>{true});
}

TEST_CASE("Feature iterator flush_tail pushes an empty flagged batch after the tail", "[features][nodes]") {
  for (const bool flush_tail : {true, false}) {
    auto node = Iterator(std::string("flush_tail: ") + (flush_tail ? "true" : "false"));

    Push(node, {1, 2, 3}, false, true);
    auto expected = std::vector<std::vector<float>>{{1, 2}, {3}};
    auto flags = std::vector<bool>{false, true};
    if (flush_tail) {
      expected.emplace_back();
      flags.push_back(true);
    }
    const auto batches = Get(node);
    REQUIRE(batches.features == expected);
    REQUIRE(batches.tails == flags);
    REQUIRE(batches.resets == std::vector<bool>(expected.size(), false));
  }
}

TEST_CASE("Feature iterator does not flush the tail by default", "[features][nodes]") {
  auto node = Iterator();
  Push(node, {1, 2, 3}, false, true);
  const auto batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{1, 2}, {3}});
  REQUIRE(batches.tails == std::vector<bool>{false, true});
}

TEST_CASE("Feature iterator flush_tail follows an empty tail too", "[features][nodes]") {
  auto node = Iterator("flush_tail: true");
  Push(node, {1, 2}, false, true);
  const auto batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{1, 2}, {}, {}});
  REQUIRE(batches.tails == std::vector<bool>{false, true, true});
  REQUIRE(batches.resets == std::vector<bool>{false, false, false});
}

TEST_CASE("Feature iterator flush_tail adds nothing without push_features", "[features][nodes]") {
  auto node = Iterator("flush_tail: true");
  Push(node, {1, 2, 3}, false);
  auto batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{1, 2}});
  REQUIRE(batches.tails == std::vector<bool>{false});

  Push(node, {}, false, true);
  batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{}, {3}, {}});
  REQUIRE(batches.tails == std::vector<bool>{false, true, true});
}

TEST_CASE("Feature iterator flush_tail keeps a pending reset across repeated resets", "[features][nodes]") {
  auto node = Iterator("flush_tail: true");

  Push(node, {}, true, true);
  auto batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{}, {}, {}});
  REQUIRE(batches.resets == std::vector<bool>{true, true, true});
  REQUIRE(batches.tails == std::vector<bool>{false, true, true});

  Push(node, {1}, true, true);
  batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{}, {1}, {}});
  REQUIRE(batches.resets == std::vector<bool>{true, true, false});

  Push(node, {2, 3}, false, true);
  batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{2, 3}, {}, {}});
  REQUIRE(batches.resets == std::vector<bool>{false, false, false});

  node.node->Reset();
  Push(node, {}, false, true);
  batches = Get(node);
  REQUIRE(batches.features == std::vector<std::vector<float>>{{}, {}, {}});
  REQUIRE(batches.resets == std::vector<bool>{true, true, true});
}

TEST_CASE("Feature iterator with flush_tail still requires the window params", "[features][nodes]") {
  REQUIRE_THROWS(CreateNode("feature_iterator_with_reset", "step_size: 2\ncontext_width: 2\nflush_tail: true"));
  REQUIRE_THROWS(CreateNode("feature_iterator_with_reset", "context_width: 2\nnum_mels: 1\nflush_tail: true"));
}

TEST_CASE("feature_iterator_with_reset flush_tail_count pushes as many empty flagged batches", "[features][nodes]") {
  auto node = CreateNode(
    "feature_iterator_with_reset",
    "step_size: 2\ncontext_width: 2\nnum_mels: 2\nflush_tail: true\nflush_tail_count: 3",
    {},
    {"push_features"},
    {},
    {},
    nullptr,
    {"push_features_batch"}
  );

  NodePush(node, "features", std::vector<float>(6, 1.0F));
  NodePush(node, "push_features", true);
  const auto expected = std::vector<std::vector<float>>{
    std::vector<float>(4, 1.0F), std::vector<float>(2, 1.0F), {}, {}, {}
  };
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "features_batch") == expected);
  REQUIRE(NodeGetBatch<bool>(node, "push_features_batch") == std::vector<bool>{false, true, true, true, true});
  REQUIRE(NodeGetBatch<bool>(node, "need_reset_batch") == std::vector<bool>(expected.size(), false));
}

TEST_CASE("feature_iterator_with_reset flush_tail_count is at least 1", "[features][nodes]") {
  for (const auto* count : {"0", "-1"}) {
    CAPTURE(count);
    REQUIRE_THROWS_WITH(
      CreateNode(
        "feature_iterator_with_reset",
        std::string("step_size: 2\ncontext_width: 2\nnum_mels: 2\nflush_tail: true\nflush_tail_count: ") + count
      ),
      Catch::Contains("'flush_tail_count' must be at least 1")
    );
  }
}
