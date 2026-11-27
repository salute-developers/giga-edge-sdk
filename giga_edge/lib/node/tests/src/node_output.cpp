#include "giga_edge/lib/node/node_output.h"

#include <catch.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace giga_edge;
using namespace std::chrono_literals;

namespace {

TEST_CASE("NodeOutput::Unsubscribe waits for an in-flight notify callback", "[runtime][node_output]") {
  auto output = std::make_shared<NodeOutput>("test_output");

  auto entered = std::make_shared<std::promise<void>>();
  auto release = std::make_shared<std::promise<void>>();
  auto release_future = std::make_shared<std::shared_future<void>>(release->get_future());

  output->Subscribe("blocker", [entered, release_future] {
    entered->set_value();
    release_future->wait();
  });

  std::thread pusher([output] {
    output->Push(true, ValueInfo{});
  });
  entered->get_future().wait();

  auto unsubscribed = std::make_shared<std::promise<void>>();
  auto unsubscribed_future = unsubscribed->get_future();
  std::thread unsubscriber([output, unsubscribed] {
    output->Unsubscribe("blocker");
    unsubscribed->set_value();
  });

  const auto status_while_held = unsubscribed_future.wait_for(100ms);
  release->set_value();
  const bool finished = unsubscribed_future.wait_for(5s) == std::future_status::ready;

  if (finished) {
    pusher.join();
    unsubscriber.join();
  } else {
    pusher.detach();
    unsubscriber.detach();
  }

  REQUIRE(status_while_held == std::future_status::timeout);
  REQUIRE(finished);
}

TEST_CASE("NodeOutput::Unsubscribe stops further notifications", "[runtime][node_output]") {
  NodeOutput output("test_output");

  int calls = 0;
  output.Subscribe("sub", [&calls] {
    ++calls;
  });

  output.Push(true, ValueInfo{});
  REQUIRE(calls == 1);

  output.Unsubscribe("sub");
  output.Push(true, ValueInfo{});
  REQUIRE(calls == 1);
}

TEST_CASE("NodeOutput::Unsubscribe is not wedged by a throwing notify callback", "[runtime][node_output]") {
  auto output = std::make_shared<NodeOutput>("test_output");

  output->Subscribe("thrower", [] {
    throw std::runtime_error("boom");
  });

  REQUIRE_THROWS_AS(output->Push(true, ValueInfo{}), std::runtime_error);

  auto done = std::make_shared<std::promise<void>>();
  auto done_future = done->get_future();
  std::thread worker([output, done] {
    output->Unsubscribe("thrower");
    done->set_value();
  });

  const bool finished = done_future.wait_for(5s) == std::future_status::ready;
  if (finished) {
    worker.join();
  } else {
    worker.detach();
  }
  REQUIRE(finished);

  REQUIRE_NOTHROW(output->Push(true, ValueInfo{}));
}

} // namespace
