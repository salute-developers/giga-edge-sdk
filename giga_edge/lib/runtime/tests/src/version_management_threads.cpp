// Version management is thread-safe: versions are added, switched and removed from several threads at once while
// data is pushed, and the graph neither stalls nor loses a tick.

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace giga_edge;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view kCoreConfig = R"(
num_threads: 2
version: test
lib_version: 1.18

submodels:
  - id: a
    inputs:
      signal: input
    outputs:
      out: out_a
  - id: b
    inputs:
      signal: input
    outputs:
      out: out_b
)";

constexpr std::string_view kVersionConfig = R"(
nodes:
  - type: logical_op
    name: producer
    inputs:
      first_input: signal
    outputs:
      output: out
    params:
      logical_op_type: not
)";

constexpr int kSwitches = 300;
constexpr int kPushes = 3000;
constexpr int kChurnVersions = 8;

IDataReaderPtr MakeReader(std::string_view text) {
  return CreateAnyPathSameSpanDataReader(Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end())));
}

SubmodelVersionChain Chain(const std::string& id, const std::string& version) {
  return {.ids_chain = {{.id = id, .version = version}}};
}

} // namespace

TEST_CASE("Version management from several threads does not stall the graph", "[runtime][version_race]") {
  const auto version_reader = MakeReader(kVersionConfig);
  Runtime model(MakeReader(kCoreConfig));

  for (const std::string id : {"a", "b"}) {
    model.AddSubmodelVersion(Chain(id, "v0"), version_reader);
    model.AddSubmodelVersion(Chain(id, "v1"), version_reader);
    model.SetActiveSubmodelVersion(Chain(id, "v0"));
  }

  std::atomic<int> received_a{0};
  std::atomic<int> received_b{0};
  model.Subscribe<bool>({}, "out_a", [&received_a](const bool&, const std::any&) {
    ++received_a;
  });
  model.Subscribe<bool>({}, "out_b", [&received_b](const bool&, const std::any&) {
    ++received_b;
  });

  std::atomic<int> finished{0};
  std::atomic<bool> switching{true};
  std::vector<std::thread> threads;

  // Two threads switch the versions of two submodels.
  for (const std::string id : {"a", "b"}) {
    threads.emplace_back([&model, &finished, id] {
      for (int i = 1; i <= kSwitches; ++i) {
        model.SetActiveSubmodelVersion(Chain(id, i % 2 ? "v1" : "v0"));
      }
      ++finished;
    });
  }

  // A third one adds and removes other versions of the same submodels.
  threads.emplace_back([&model, &finished, &switching, &version_reader] {
    while (switching.load()) {
      for (int v = 0; v < kChurnVersions; ++v) {
        model.AddSubmodelVersion(Chain(v % 2 ? "a" : "b", "churn" + std::to_string(v)), version_reader);
      }
      for (int v = 0; v < kChurnVersions; ++v) {
        model.RemoveSubmodelVersion(Chain(v % 2 ? "a" : "b", "churn" + std::to_string(v)));
      }
    }
    ++finished;
  });

  // The host keeps pushing data meanwhile.
  threads.emplace_back([&model, &finished] {
    for (int i = 0; i < kPushes; ++i) {
      model.Push<bool>({}, "input", i % 2 == 0, {});
    }
    ++finished;
  });

  // A stalled graph blocks the switching threads forever and they cannot be joined: fail instead of hanging.
  const auto threads_deadline = std::chrono::steady_clock::now() + 60s;
  while (finished.load() < 3) {
    if (std::chrono::steady_clock::now() > threads_deadline) {
      std::cerr << "version management threads are stuck: the graph has stalled\n";
      std::abort();
    }
    std::this_thread::sleep_for(1ms);
  }
  switching = false;
  for (auto& thread : threads) {
    thread.join();
  }

  const auto deadline = std::chrono::steady_clock::now() + 30s;
  while ((received_a.load() < kPushes || received_b.load() < kPushes) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  REQUIRE(received_a.load() == kPushes);
  REQUIRE(received_b.load() == kPushes);
}

TEST_CASE("Removing the active version waits for another thread to switch the submodel", "[runtime][version_race]") {
  const auto version_reader = MakeReader(kVersionConfig);
  Runtime model(MakeReader(kCoreConfig));

  model.AddSubmodelVersion(Chain("a", "v0"), version_reader);
  model.AddSubmodelVersion(Chain("a", "v1"), version_reader);
  model.SetActiveSubmodelVersion(Chain("a", "v0"));

  std::atomic<bool> removed{false};
  std::thread remover([&model, &removed] {
    model.RemoveSubmodelVersion(Chain("a", "v0"));
    removed = true;
  });

  // The removal is waiting for the nodes of v0 to become inactive; neither order of the two calls may hang.
  std::this_thread::sleep_for(20ms);
  model.SetActiveSubmodelVersion(Chain("a", "v1"));

  const auto deadline = std::chrono::steady_clock::now() + 60s;
  while (!removed.load()) {
    if (std::chrono::steady_clock::now() > deadline) {
      std::cerr << "the removal is stuck although another version is active\n";
      std::abort();
    }
    std::this_thread::sleep_for(1ms);
  }
  remover.join();

  REQUIRE_FALSE(model.HasOutput(Chain("a", "v0"), "out"));
  REQUIRE(model.CollectConsumedVersions().at("a") == "v1");
}
