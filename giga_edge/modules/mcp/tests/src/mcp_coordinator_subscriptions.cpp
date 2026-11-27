#include "giga_edge/modules/mcp/coordinator.h"
#include "giga_edge/modules/mcp/tests/mcp_test_utils/fake_mcp_service.h"

#include <catch.hpp>

#include <memory>

using namespace giga_edge::mcp;
using giga_edge::mcp::test_utils::FakeMcpService;

namespace {

const McpResourceSubscriptionState& RequireSingleSubscriptionState(
  const VersionedSnapshot<std::vector<McpResourceSubscriptionState>>& snapshot
) {
  REQUIRE(snapshot.data);
  REQUIRE(snapshot.data->size() == 1);
  return snapshot.data->at(0);
}

} // namespace

TEST_CASE(
  "MCP coordinator keeps latest-only resource states after successful subscription", "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_resource",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));

  auto subscription_states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  const auto& state = RequireSingleSubscriptionState(subscription_states);
  REQUIRE(state.server_id == "alpha");
  REQUIRE(state.uri == "resource://clock");
  REQUIRE(state.status == McpResourceSubscriptionStatus::SUBSCRIBED);

  auto initial_snapshot = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(initial_snapshot.data);
  REQUIRE(initial_snapshot.data->empty());

  Json::Value first(Json::objectValue);
  first["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", first);

  auto resource_snapshot = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(resource_snapshot.data->size() == 1);
  REQUIRE(resource_snapshot.data->at(0).data["data"]["value"].asString() == "10:00");

  Json::Value second(Json::objectValue);
  second["value"] = "10:01";
  service->EmitResourceUpdate("alpha", "resource://clock", second);

  resource_snapshot = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(resource_snapshot.data->size() == 1);
  REQUIRE(resource_snapshot.data->at(0).data["data"]["value"].asString() == "10:01");
}

TEST_CASE(
  "MCP coordinator exposes shared latest resource states without consumer subscription",
  "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "scene",
      .visible_server_ids = {"alpha"},
      .include_shared_latest_resource_states = true,
    }
  );
  const auto private_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "private",
      .visible_server_ids = {"alpha"},
    }
  );

  Json::Value update(Json::objectValue);
  update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", update);

  const auto shared_latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(shared_latest.data);
  REQUIRE(shared_latest.data->size() == 1);
  REQUIRE(shared_latest.data->at(0).server_id == "alpha");
  REQUIRE(shared_latest.data->at(0).uri == "resource://clock");
  REQUIRE(shared_latest.data->at(0).data["data"]["value"].asString() == "10:00");

  const auto private_latest = coordinator->GetLatestResourceStatesSnapshot(private_consumer);
  REQUIRE(private_latest.data);
  REQUIRE(private_latest.data->empty());
}

TEST_CASE(
  "MCP coordinator seeds latest resource states for a consumer registered after the update",
  "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  Json::Value update(Json::objectValue);
  update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", update);

  const auto late_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "scene",
      .visible_server_ids = {"alpha"},
      .include_shared_latest_resource_states = true,
    }
  );

  const auto latest = coordinator->GetLatestResourceStatesSnapshot(late_consumer);
  REQUIRE(latest.data);
  REQUIRE(latest.data->size() == 1);
  REQUIRE(latest.data->at(0).server_id == "alpha");
  REQUIRE(latest.data->at(0).uri == "resource://clock");
  REQUIRE(latest.data->at(0).data["data"]["value"].asString() == "10:00");
}

TEST_CASE(
  "MCP coordinator does not seed latest resource states that a consumer must not see",
  "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  Json::Value update(Json::objectValue);
  update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", update);

  const auto private_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "private",
      .visible_server_ids = {"alpha"},
    }
  );
  const auto other_server_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "beta_scene",
      .visible_server_ids = {"beta"},
      .include_shared_latest_resource_states = true,
    }
  );

  const auto private_latest = coordinator->GetLatestResourceStatesSnapshot(private_consumer);
  REQUIRE(private_latest.data);
  REQUIRE(private_latest.data->empty());

  const auto beta_latest = coordinator->GetLatestResourceStatesSnapshot(other_server_consumer);
  REQUIRE(beta_latest.data);
  REQUIRE(beta_latest.data->empty());
}

TEST_CASE(
  "MCP coordinator removes shared latest states for resources removed from catalog", "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  service->resources_snapshot = {
    {.server_id = "alpha", .uri = "resource://clock"},
    {.server_id = "alpha", .uri = "resource://weather"},
  };
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "scene",
      .visible_server_ids = {"alpha"},
      .include_shared_latest_resource_states = true,
    }
  );

  Json::Value clock(Json::objectValue);
  clock["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", clock);
  Json::Value weather(Json::objectValue);
  weather["value"] = "sunny";
  service->EmitResourceUpdate("alpha", "resource://weather", weather);

  auto latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(latest.data->size() == 2);

  service->EmitResources({{.server_id = "alpha", .uri = "resource://clock"}});

  latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(latest.data);
  REQUIRE(latest.data->size() == 1);
  REQUIRE(latest.data->at(0).uri == "resource://clock");
}

TEST_CASE("MCP coordinator shares one physical subscription between consumers", "[mcp][coordinator][subscriptions]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto first_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "first",
      .visible_server_ids = {"alpha"},
    }
  );
  const auto second_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "second",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE(coordinator->SubscribeResource(first_consumer, "alpha", "resource://clock"));
  REQUIRE(coordinator->SubscribeResource(second_consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 1);
  REQUIRE(service->subscription_operation_calls[0].operation == McpResourceSubscriptionEvent::Operation::SUBSCRIBE);

  coordinator->UnsubscribeResource(first_consumer, "alpha", "resource://clock");
  REQUIRE(service->subscription_operation_calls.size() == 1);

  coordinator->UnsubscribeResource(second_consumer, "alpha", "resource://clock");
  REQUIRE(service->subscription_operation_calls.size() == 2);
  REQUIRE(service->subscription_operation_calls[1].operation == McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE);
}

TEST_CASE(
  "MCP coordinator rejects invisible subscriptions and deduplicates repeated subscribe",
  "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_only",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE_FALSE(coordinator->SubscribeResource(consumer, "beta", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.empty());

  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));
  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 1);
}

TEST_CASE("MCP coordinator exposes failed subscribe state and hides latest data", "[mcp][coordinator][subscriptions]") {
  auto service = std::make_shared<FakeMcpService>();
  service->auto_complete_subscriptions = false;

  auto coordinator = CreateMcpCoordinator(service);
  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_resource",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 1);

  auto subscription_states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  REQUIRE(
    RequireSingleSubscriptionState(subscription_states).status == McpResourceSubscriptionStatus::SUBSCRIBE_PENDING
  );

  const auto operation_id = service->subscription_operation_calls[0].operation_id;
  service->EmitResourceSubscriptionResult(
    McpResourceSubscriptionEvent{
      .operation_id = operation_id,
      .server_id = "alpha",
      .uri = "resource://clock",
      .operation = McpResourceSubscriptionEvent::Operation::SUBSCRIBE,
      .is_error = true,
      .error_message = "subscribe failed",
    }
  );

  subscription_states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  const auto& state = RequireSingleSubscriptionState(subscription_states);
  REQUIRE(state.status == McpResourceSubscriptionStatus::SUBSCRIBE_FAILED);
  REQUIRE(state.error_message == "subscribe failed");

  Json::Value update(Json::objectValue);
  update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", update);

  const auto latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  REQUIRE(latest.data);
  REQUIRE(latest.data->empty());
}

TEST_CASE("MCP coordinator clears subscription-related state on disconnect", "[mcp][coordinator][subscriptions]") {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_resource",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));

  Json::Value update(Json::objectValue);
  update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", update);

  auto latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  auto states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  REQUIRE(latest.data->size() == 1);
  REQUIRE(states.data->size() == 1);

  service->EmitServerDisconnected("alpha");

  latest = coordinator->GetLatestResourceStatesSnapshot(consumer);
  states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  REQUIRE(latest.data);
  REQUIRE(latest.data->empty());
  REQUIRE(states.data);
  REQUIRE(states.data->empty());
}

TEST_CASE(
  "MCP coordinator marks subscribe as failed when service rejects operation", "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  service->accept_subscriptions = false;
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "alpha_resource",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE_FALSE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 1);

  const auto states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  const auto& state = RequireSingleSubscriptionState(states);
  REQUIRE(state.status == McpResourceSubscriptionStatus::SUBSCRIBE_FAILED);
}

TEST_CASE(
  "MCP coordinator unsubscribes removed servers after consumer visibility update", "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "all_servers",
    }
  );

  REQUIRE(coordinator->SubscribeResource(consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 1);
  REQUIRE(service->subscription_operation_calls[0].operation == McpResourceSubscriptionEvent::Operation::SUBSCRIBE);

  coordinator->UpdateConsumerConfig(
    consumer,
    McpConsumerConfig{
      .consumer_name = "beta_only",
      .visible_server_ids = {"beta"},
    }
  );

  REQUIRE(service->subscription_operation_calls.size() == 2);
  REQUIRE(service->subscription_operation_calls[1].operation == McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE);
  REQUIRE(service->subscription_operation_calls[1].server_id == "alpha");
  REQUIRE(service->subscription_operation_calls[1].uri == "resource://clock");

  const auto states = coordinator->GetResourceSubscriptionStatesSnapshot(consumer);
  REQUIRE(states.data);
  REQUIRE(states.data->empty());
}

TEST_CASE(
  "MCP coordinator keeps shared subscription active for remaining consumer", "[mcp][coordinator][subscriptions]"
) {
  auto service = std::make_shared<FakeMcpService>();
  auto coordinator = CreateMcpCoordinator(service);

  const auto first_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "first",
      .visible_server_ids = {"alpha"},
    }
  );
  const auto second_consumer = coordinator->RegisterConsumer(
    McpConsumerConfig{
      .consumer_name = "second",
      .visible_server_ids = {"alpha"},
    }
  );

  REQUIRE(coordinator->SubscribeResource(first_consumer, "alpha", "resource://clock"));
  REQUIRE(coordinator->SubscribeResource(second_consumer, "alpha", "resource://clock"));
  REQUIRE(service->subscription_operation_calls.size() == 1);

  coordinator->UnsubscribeResource(first_consumer, "alpha", "resource://clock");
  REQUIRE(service->subscription_operation_calls.size() == 1);

  Json::Value update(Json::objectValue);
  update["value"] = "10:00";
  service->EmitResourceUpdate("alpha", "resource://clock", update);

  const auto first_latest = coordinator->GetLatestResourceStatesSnapshot(first_consumer);
  const auto second_latest = coordinator->GetLatestResourceStatesSnapshot(second_consumer);
  REQUIRE(first_latest.data);
  REQUIRE(first_latest.data->empty());
  REQUIRE(second_latest.data);
  REQUIRE(second_latest.data->size() == 1);
  REQUIRE(second_latest.data->at(0).data["data"]["value"].asString() == "10:00");

  coordinator->UnsubscribeResource(second_consumer, "alpha", "resource://clock");
  REQUIRE(service->subscription_operation_calls.size() == 2);
  REQUIRE(service->subscription_operation_calls[1].operation == McpResourceSubscriptionEvent::Operation::UNSUBSCRIBE);
}
