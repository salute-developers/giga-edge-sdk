#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

#include <memory>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeMcpService;

TEST_CASE("MCP coordinator filters snapshots by visible servers", "[mcp][coordinator][snapshots]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {
    {.server_id = "alpha", .tool_name = "search"},
    {.server_id = "beta", .tool_name = "calendar"},
  };
  service->resources_snapshot = {
    {.server_id = "alpha", .uri = "resource://a", .name = "A"},
    {.server_id = "beta", .uri = "resource://b", .name = "B"},
  };
  service->resource_templates_snapshot = {
    {.server_id = "alpha", .uri_template = "template://a", .name = "A"},
    {.server_id = "beta", .uri_template = "template://b", .name = "B"},
  };

  auto coordinator = CreateMcpCoordinator(service);
  const auto alpha_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_only",
      .visible_server_ids = {"alpha"},
    }
  );
  const auto all_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "all",
    }
  );

  auto alpha_tools = coordinator->GetToolsSnapshot(alpha_consumer);
  auto alpha_resources = coordinator->GetResourcesSnapshot(alpha_consumer);
  auto alpha_templates = coordinator->GetResourceTemplatesSnapshot(alpha_consumer);
  REQUIRE(alpha_tools.version == 1);
  REQUIRE(alpha_resources.version == 1);
  REQUIRE(alpha_templates.version == 1);
  REQUIRE(alpha_tools.data->size() == 1);
  REQUIRE(alpha_resources.data->size() == 1);
  REQUIRE(alpha_templates.data->size() == 1);
  REQUIRE(alpha_tools.data->at(0).server_id == "alpha");
  REQUIRE(alpha_resources.data->at(0).server_id == "alpha");
  REQUIRE(alpha_templates.data->at(0).server_id == "alpha");

  auto all_tools = coordinator->GetToolsSnapshot(all_consumer);
  auto all_resources = coordinator->GetResourcesSnapshot(all_consumer);
  auto all_templates = coordinator->GetResourceTemplatesSnapshot(all_consumer);
  REQUIRE(all_tools.version == 1);
  REQUIRE(all_resources.version == 1);
  REQUIRE(all_templates.version == 1);
  REQUIRE(all_tools.data->size() == 2);
  REQUIRE(all_resources.data->size() == 2);
  REQUIRE(all_templates.data->size() == 2);
}

TEST_CASE("MCP coordinator increments versions only for affected consumers", "[mcp][coordinator][snapshots]") {
  auto service = std::make_shared<FakeMcpService>();
  service->tools_snapshot = {
    {.server_id = "alpha", .tool_name = "search"},
    {.server_id = "beta", .tool_name = "calendar"},
  };

  auto coordinator = CreateMcpCoordinator(service);
  const auto alpha_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_only",
      .visible_server_ids = {"alpha"},
    }
  );
  const auto all_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "all",
    }
  );

  auto alpha_tools = coordinator->GetToolsSnapshot(alpha_consumer);
  auto all_tools = coordinator->GetToolsSnapshot(all_consumer);
  REQUIRE(alpha_tools.version == 1);
  REQUIRE(all_tools.version == 1);

  service->EmitTools({
    {.server_id = "alpha", .tool_name = "search"},
    {.server_id = "beta", .tool_name = "calendar"},
    {.server_id = "beta", .tool_name = "email"},
  });

  alpha_tools = coordinator->GetToolsSnapshot(alpha_consumer);
  all_tools = coordinator->GetToolsSnapshot(all_consumer);
  REQUIRE(alpha_tools.version == 1);
  REQUIRE(alpha_tools.data->size() == 1);
  REQUIRE(all_tools.version == 2);
  REQUIRE(all_tools.data->size() == 3);
}

TEST_CASE("MCP coordinator updates snapshots after consumer visibility change", "[mcp][coordinator][snapshots]") {
  auto service = std::make_shared<FakeMcpService>();
  service->resources_snapshot = {
    {.server_id = "alpha", .uri = "resource://a", .name = "A"},
    {.server_id = "beta", .uri = "resource://b", .name = "B"},
  };

  auto coordinator = CreateMcpCoordinator(service);
  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_only",
      .visible_server_ids = {"alpha"},
    }
  );

  auto resources = coordinator->GetResourcesSnapshot(consumer);
  REQUIRE(resources.version == 1);
  REQUIRE(resources.data->size() == 1);
  REQUIRE(resources.data->at(0).server_id == "alpha");

  coordinator->UpdateConsumerConfig(
    consumer,
    McpConsumerConfig{
      .consumer_name = "all",
    }
  );

  resources = coordinator->GetResourcesSnapshot(consumer);
  REQUIRE(resources.version == 2);
  REQUIRE(resources.data->size() == 2);
}
