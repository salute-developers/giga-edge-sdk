#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/modules/mcp/environment.h"
#include "giga_edge/modules/mcp/types.h"

#include <catch.hpp>

#include <memory>
#include <stdexcept>

using namespace giga_edge::mcp;
using namespace giga_edge;

TEST_CASE("MCP environment initializes service and coordinator eagerly", "[mcp][environment]") {
  McpServiceEnvironment environment(McpServiceConfig{});

  REQUIRE(environment.service);
  REQUIRE(environment.coordinator);
}

TEST_CASE("MCP environment is registered in the external-environment registry", "[mcp][environment]") {
  // The "mcp" type is registered by a static initializer of mcp_module: the test checks that it is linked in.
  const auto* handlers = giga_edge::FindExternalEnvironment("mcp");
  REQUIRE(handlers != nullptr);
  REQUIRE(handlers->make);

  giga_edge::EnvironmentParams params;
  params.type = giga_edge::EnvironmentParams::Type::EXTERNAL;
  params.external_type = "mcp";
  params.external_config = std::make_shared<McpServiceConfig>();

  const giga_edge::EnvironmentPtr env = giga_edge::MakeEnvironment(params);
  REQUIRE(env);
  REQUIRE(dynamic_cast<McpServiceEnvironment*>(env.get()) != nullptr);
}

TEST_CASE("MCP environment parses resource hydration retry settings", "[mcp][environment][config]") {
  auto yaml = YAML::Load(R"(
    model_environments:
      mcp:
        type: mcp
        mcp:
          resource_hydration: {}
  )");
  auto hydration = yaml["model_environments"]["mcp"]["mcp"]["resource_hydration"];
  auto expected = McpResourceHydrationConfig{};
  SECTION("omitted retry settings keep defaults") {}
  SECTION("custom error codes replace defaults") {
    hydration["retry_initial_ms"] = 25;
    hydration["retry_max_ms"] = 90;
    hydration["no_retry_error_codes"] = YAML::Load("[-31901, -32000]");
    expected.retry_initial_ms = 25;
    expected.retry_max_ms = 90;
    expected.no_retry_error_codes = {-31901, -32000};
  }
  SECTION("empty error codes allow retrying all errors") {
    hydration["no_retry_error_codes"] = YAML::Load("[]");
    expected.no_retry_error_codes.clear();
  }
  SECTION("zero initial pause disables retries") {
    hydration["retry_initial_ms"] = 0;
    hydration["retry_max_ms"] = 0;
    expected.retry_initial_ms = 0;
    expected.retry_max_ms = 0;
  }
  SECTION("maximum pause accepts the uint32 boundary") {
    hydration["retry_max_ms"] = uint64_t{4294967295};
    expected.retry_max_ms = 4294967295u;
  }
  const auto params = ParseConfig<EnvironmentsMapParams>(yaml).at("mcp");
  REQUIRE(params.type == EnvironmentParams::Type::EXTERNAL);
  const auto& parsed = std::static_pointer_cast<McpServiceConfig>(params.external_config)->resource_hydration;
  REQUIRE(parsed.retry_initial_ms == expected.retry_initial_ms);
  REQUIRE(parsed.retry_max_ms == expected.retry_max_ms);
  REQUIRE(parsed.no_retry_error_codes == expected.no_retry_error_codes);
}

TEST_CASE("MCP environment rejects a thread pool without threads", "[mcp][environment][config]") {
  const auto size = GENERATE(as<std::string>{}, "0", "4294967295");
  CAPTURE(size);
  const auto config = ParseMcpServiceConfig(YAML::Load("thread_pool_size: " + size));
  REQUIRE_THROWS_AS(McpServiceEnvironment(config), std::invalid_argument);
  REQUIRE_NOTHROW(McpServiceEnvironment(ParseMcpServiceConfig(YAML::Load("thread_pool_size: 1"))));
}

TEST_CASE("MCP environment accepts the removed read_resource_on_update key", "[mcp][environment][config]") {
  const auto config = ParseMcpServiceConfig(YAML::Load(R"(
    resource_hydration:
      read_resources_on_list: true
      read_resource_on_update: true
  )"));
  REQUIRE(config.resource_hydration.read_resources_on_list);
  REQUIRE_NOTHROW(McpServiceEnvironment(config));
}
