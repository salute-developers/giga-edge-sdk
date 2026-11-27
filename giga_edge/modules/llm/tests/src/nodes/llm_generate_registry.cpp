#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/llm/nodes.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("llm_generate is registered with a batch stream output and optional report", "[llm][nodes][generate]") {
  const auto& info = NodeRegistry::GetNodeInfoMap().at("llm_generate");
  REQUIRE(info.inputs.count("assistant_request") == 1);
  REQUIRE_FALSE(info.inputs.at("assistant_request").is_batch);
  REQUIRE(info.outputs.at("stream_chunk").is_batch);
  REQUIRE(info.outputs.at("llm_report").is_batch);
  REQUIRE(info.outputs.at("llm_report").is_optional);
}
