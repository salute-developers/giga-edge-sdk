#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/common/nodes/batchify/node.h"

#include <catch.hpp>

using namespace giga_edge;

namespace {

template<class T>
void PushInputWithInfo(NodeContext& node_ctx, const std::string& input, const T& data, const ValueInfo& value_info) {
  node_ctx.inputs.at(input)->Push(data, value_info);
  while (node_ctx.node->GetNextProcessingType() != NextProcessingType::NONE) {
    node_ctx.node->Process();
  }
}

} // namespace

TEST_CASE("Batchify node repeats value for each batch item", "[runtime][nodes]") {
  auto node = CreateNode("batchify", "");

  PushInputWithInfo(
    node,
    "value",
    5.f,
    ValueInfo{
      .seq_id = 0,
    }
  );
  PushInputWithInfo(
    node,
    "batch",
    std::vector<float>{1.f, 2.f},
    ValueInfo{
      .batch_infos = {{.id = 0, .is_last = false}},
      .seq_id = 0,
    }
  );
  PushInputWithInfo(
    node,
    "batch",
    std::vector<float>{3.f, 4.f},
    ValueInfo{
      .batch_infos = {{.id = 0, .is_last = false}},
      .seq_id = 0,
    }
  );
  PushInputWithInfo(
    node,
    "batch",
    std::vector<float>{5.f},
    ValueInfo{
      .batch_infos = {{.id = 0, .is_last = true}},
      .seq_id = 0,
    }
  );

  REQUIRE(NodeGetBatch<std::vector<float>>(node, "output") == std::vector<std::vector<float>>{{5.f}, {5.f}, {5.f}});
}

TEST_CASE("Batchify node handles consecutive batches", "[runtime][nodes]") {
  auto node = CreateNode("batchify", "");

  PushInputWithInfo(
    node,
    "value",
    2.f,
    ValueInfo{
      .seq_id = 0,
    }
  );
  PushInputWithInfo(
    node,
    "batch",
    std::vector<float>{10.f},
    ValueInfo{
      .batch_infos = {{.id = 0, .is_last = false}},
      .seq_id = 0,
    }
  );
  PushInputWithInfo(
    node,
    "batch",
    std::vector<float>{11.f, 12.f},
    ValueInfo{
      .batch_infos = {{.id = 0, .is_last = true}},
      .seq_id = 0,
    }
  );
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "output") == std::vector<std::vector<float>>{{2.f}, {2.f}});

  PushInputWithInfo(
    node,
    "value",
    -3.f,
    ValueInfo{
      .seq_id = 1,
    }
  );
  PushInputWithInfo(
    node,
    "batch",
    std::vector<float>{13.f, 14.f, 15.f},
    ValueInfo{
      .batch_infos = {{.id = 1, .is_last = true}},
      .seq_id = 1,
    }
  );
  REQUIRE(NodeGetBatch<std::vector<float>>(node, "output") == std::vector<std::vector<float>>{{-3.f}});
}
