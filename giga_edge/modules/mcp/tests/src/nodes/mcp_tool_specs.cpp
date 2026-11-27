#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/mcp/nodes/mcp_tool_specs/node.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("mcp_tool_specs maps catalog tools one to one", "[mcp][nodes][tool_specs]") {
  auto node = CreateNode("mcp_tool_specs", "");
  McpCatalogSnapshot snapshot;
  snapshot.revision = "rev-7";
  snapshot.tools.push_back({"home", "set_volume", "Sets volume", R"({"type":"object"})"});
  snapshot.tools.push_back({"home", "get_volume", "Reads volume", ""});
  NodePush(node, "mcp_catalog_snapshot", snapshot);
  const auto specs = NodeGet<ToolSpecs>(node, "tool_specs");
  REQUIRE(specs.status == AssistantStatus::OK);
  REQUIRE(specs.revision == "rev-7");
  REQUIRE(specs.tools.size() == 2);
  REQUIRE(specs.tools[0].name == "set_volume");
  REQUIRE(specs.tools[0].description == "Sets volume");
  REQUIRE(specs.tools[0].parameters_json == R"({"type":"object"})");
  REQUIRE(specs.tools[1].parameters_json.empty());
}
