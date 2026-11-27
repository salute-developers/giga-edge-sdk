// Submodels of one level load in parallel (load_threads in the core config): the result is the same as one by one.

#include "matchers.h"

#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/tests/runtime_test_utils/temp_dir.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace giga_edge;
using giga_edge::test::TempDir;
using namespace std::chrono_literals;

namespace {

std::atomic<int> g_loading{0};
std::atomic<int> g_max_loading{0};
std::atomic<bool> g_wait_for_peer{false}; // parallel load: the probe waits for a peer instead of relying on sleep
std::mutex g_created_mutex;
std::vector<std::string> g_created; // names of the probe nodes in creation order

// A node that takes long to create; with the name broken it is not created at all.
class LoadProbeNode : public NodeBase {
public:
  explicit LoadProbeNode(const NodeBaseConfig& config)
    : NodeBase(config) {
    {
      const std::lock_guard lock(g_created_mutex);
      g_created.push_back(config.base_id);
    }
    const int now = ++g_loading;
    for (int seen = g_max_loading.load(); now > seen && !g_max_loading.compare_exchange_weak(seen, now);) {
    }
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    // The root loads before everyone else and has no one to wait for.
    while (g_wait_for_peer && config.base_id != "core_probe" && g_max_loading < 2
           && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(20ms);
    --g_loading;
    if (config.base_id == "broken") {
      throw std::runtime_error("broken probe");
    }
  }

  void Reset() override {}

  void ProcessOnce() override {
    PushOutput(GetInput());
  }

  REGISTER_INPUT(parallel_load_probe, input, Input, bool, "");
  REGISTER_OUTPUT(parallel_load_probe, output, Output, bool, "");
};

REGISTER_NODE(parallel_load_probe, LoadProbeNode, "");

std::atomic<int> g_faults_started{0};
std::atomic<bool> g_faults_meet{false};

struct LoadAbort {};

// A node that fails to be created: abort throws something that is not a std::exception, the others throw
// std::runtime_error. With g_faults_meet it waits for a second such node: both are certainly started at once.
class LoadFaultNode : public NodeBase {
public:
  explicit LoadFaultNode(const NodeBaseConfig& config)
    : NodeBase(config) {
    {
      const std::lock_guard lock(g_created_mutex);
      g_created.push_back(config.base_id);
    }
    ++g_faults_started;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (g_faults_meet && g_faults_started < 2 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    if (config.base_id == "abort") {
      throw LoadAbort{};
    }
    throw std::runtime_error("faulty probe");
  }

  void Reset() override {}

  void ProcessOnce() override {
    PushOutput(GetInput());
  }

  REGISTER_INPUT(parallel_load_fault, input, Input, bool, "");
  REGISTER_OUTPUT(parallel_load_fault, output, Output, bool, "");
};

REGISTER_NODE(parallel_load_fault, LoadFaultNode, "");

struct Version {
  std::vector<std::pair<std::string, std::string>> children; // submodel, its default version (empty: by feature only)
  std::vector<std::pair<std::string, std::string>> features; // feature, submodel version when it is on
  std::string probe = "probe";
  std::string node = "parallel_load_probe";
};

// Each version: a probe node and a logical AND over the outputs of the child submodels.
std::string VersionConfig(const std::string& name, const Version& version, int load_threads) {
  std::ostringstream out;
  out << "version: " << name << "\n";
  if (name == "core") {
    out << "num_threads: 1\nload_threads: " << load_threads << "\nlib_version: 1.18\n";
  }
  std::vector<std::string> variables{"probed"};
  if (!version.children.empty()) {
    out << "submodels:\n";
    for (const auto& [child, _] : version.children) {
      out << "  - id: " << child << "\n    inputs: {input: input}\n    outputs: {output: " << child << "_out}\n";
      variables.push_back(child + "_out");
    }
    out << "default_submodels:\n";
    for (const auto& [child, child_version] : version.children) {
      if (!child_version.empty()) {
        out << "  " << child << ": " << child_version << "\n";
      }
    }
  }
  if (!version.features.empty()) {
    out << "features:\n";
    for (const auto& [feature, child_version] : version.features) {
      out << "  - feature_ids: [" << feature << "]\n    submodel_id: " << feature
          << "\n    submodel_version: " << child_version << "\n";
    }
  }
  out << "nodes:\n  - type: " << version.node << "\n    name: " << version.probe
      << "\n    inputs: {input: input}\n    outputs: {output: probed}\n"
      << "  - type: logical_expression\n    name: pass_through\n    inputs:\n";
  std::string expression;
  for (const auto& variable : variables) {
    out << "      " << variable << ": " << variable << "\n";
    expression += (expression.empty() ? "" : " && ") + variable;
  }
  out << "    outputs: {result: output}\n    params: {expression: '" << expression << "'}\n";
  return out.str();
}

// core -> assistant (a, b, c -> c1, d), speech; features a and b switch them to a2 and b2, feature d switches to
// d_broken (which does not load).
const std::map<std::string, Version> kVersions{
  {"core", {.children = {{"assistant", "assistant"}, {"speech", "speech"}}, .probe = "core_probe"}},
  {"speech", {}},
  {"assistant",
   {.children = {{"a", "a"}, {"b", "b"}, {"c", "c"}, {"d", "d"}},
    .features = {{"a", "a2"}, {"b", "b2"}, {"d", "d_broken"}}}},
  {"a", {}},
  {"a2", {.probe = "a2_probe"}},
  {"b", {}},
  {"b2", {.probe = "b2_probe"}},
  {"c", {.children = {{"c1", "c1"}}}},
  {"c1", {}},
  {"d", {}},
  {"d_broken", {.probe = "broken"}},
};

// Five submodels s0..s4, s<broken> is broken: directly under the core or under an intermediate p. The traversal
// order comes from an unordered_map, so the broken one is tried in every position.
std::map<std::string, Version> OneBroken(int broken, bool nested) {
  std::map<std::string, Version> versions;
  std::vector<std::pair<std::string, std::string>> siblings;
  for (int i = 0; i < 5; ++i) {
    const auto name = "s" + std::to_string(i);
    siblings.emplace_back(name, name);
    versions[name] = {.probe = i == broken ? "broken" : name};
  }
  if (nested) {
    versions["core"] = {.children = {{"p", "p"}}};
    versions["p"] = {.children = siblings};
  } else {
    versions["core"] = {.children = siblings};
  }
  return versions;
}

// x and y come only by features, without defaults: y is not built, and the whole switch fails.
const std::map<std::string, Version> kFeatureOnly{
  {"core", {.children = {{"x", ""}, {"y", ""}}, .features = {{"x", "x"}, {"y", "y_broken"}}, .probe = "core_probe"}},
  {"x", {}},
  {"y_broken", {.probe = "broken"}},
};

// p comes by a feature; with feature q the submodel p needs version q_missing, which is neither in the parts nor in
// the defaults: building p throws.
const std::map<std::string, Version> kNestedThrow{
  {"core", {.children = {{"p", ""}}, .features = {{"p", "p"}}, .probe = "core_probe"}},
  {"p", {.children = {{"c", "c"}, {"q", ""}}, .features = {{"q", "q_missing"}}}},
  {"c", {}},
};

struct Graph {
  TempDir dir;
  std::unique_ptr<RuntimeManager> manager;
};

std::unique_ptr<Graph> MakeGraph(const std::map<std::string, Version>& versions, int load_threads) {
  auto graph = std::make_unique<Graph>();
  std::vector<RuntimePart> parts;
  std::optional<RuntimePart> core;
  for (const auto& [name, version] : versions) {
    graph->dir.Write(name + "/config.yaml", VersionConfig(name, version, load_threads));
    RuntimePart part{
      .id = name,
      .data = ModelFileData{(graph->dir.path / name).string()},
      .versions = {{name, {.config_path = "config.yaml"}}},
      .permanent = true, // feature versions are not removed as unused by the very first UpdateState
    };
    if (name == "core") {
      core = std::move(part);
    } else {
      parts.push_back(std::move(part));
    }
  }
  graph->manager = std::make_unique<RuntimeManager>(*core, parts);
  return graph;
}

struct Run {
  UpdateAction initial;
  UpdateAction switched;
  UpdateAction broken_feature;
  int max_loading = 0;
  int switch_max_loading = 0;
  std::vector<std::string> switch_created;
  std::vector<std::string> broken_created;
};

Run Load(int load_threads) {
  g_max_loading = 0;
  g_wait_for_peer = load_threads > 1;
  const auto graph = MakeGraph(kVersions, load_threads);
  Run run;
  run.initial = graph->manager->InitialUpdateState();
  run.max_loading = g_max_loading;
  g_max_loading = 0;
  g_created.clear();
  run.switched = graph->manager->SetFeatures({"a", "b"});
  run.switch_max_loading = g_max_loading;
  run.switch_created = g_created;
  g_created.clear();
  run.broken_feature = graph->manager->AddFeature("d");
  run.broken_created = g_created;
  g_wait_for_peer = false;
  return run;
}

} // namespace

TEST_CASE("Sibling submodels load in parallel with the same result", "[runtime][submodel_manager]") {
  const auto sequential = Load(1);
  const auto parallel = Load(4);

  REQUIRE(sequential.max_loading == 1);
  REQUIRE(parallel.max_loading > 1);

  REQUIRE(sequential.initial.freed_parts.empty());
  REQUIRE(parallel.initial == sequential.initial);

  // a and b switch from the old versions to a2 and b2, at the same time if there is more than one thread.
  for (const auto* run : {&sequential, &parallel}) {
    REQUIRE_THAT(
      run->switch_created, Catch::Matchers::UnorderedEquals(std::vector<std::string>{"a2_probe", "b2_probe"})
    );
  }
  REQUIRE(sequential.switch_max_loading == 1);
  REQUIRE(parallel.switch_max_loading > 1);
  REQUIRE(parallel.switched == sequential.switched);

  // d_broken is not built: d stays on the old version.
  REQUIRE(sequential.broken_created == std::vector<std::string>{"broken"});
  REQUIRE(parallel.broken_created == std::vector<std::string>{"broken"});
  const auto active = [](const UpdateAction& action, const std::string& version) {
    return std::any_of(action.consumed_versions.begin(), action.consumed_versions.end(), [&](const auto& slot) {
      return slot.second == version;
    });
  };
  REQUIRE(active(sequential.switched, "a2"));
  REQUIRE(active(sequential.switched, "b2"));
  REQUIRE(active(sequential.broken_feature, "d"));
  REQUIRE_FALSE(active(sequential.broken_feature, "d_broken"));
  REQUIRE(parallel.broken_feature == sequential.broken_feature);
}

TEST_CASE("After a submodel fails the next ones are not started", "[runtime][submodel_manager]") {
  for (const bool nested : {false, true}) {
    for (int broken = 0; broken < 5; ++broken) {
      CAPTURE(nested, broken);
      g_created.clear();
      // One by one, as in a plain loop: nothing is created after the broken one.
      REQUIRE_THROWS_WITH(MakeGraph(OneBroken(broken, nested), 1), "Failure on switch to new submodel version");
      REQUIRE(g_created.back() == "broken");
      // In parallel: the same failure.
      REQUIRE_THROWS_WITH(MakeGraph(OneBroken(broken, nested), 4), "Failure on switch to new submodel version");
    }
  }
}

TEST_CASE("Load threads below one are rejected", "[runtime][submodel_manager]") {
  REQUIRE_THROWS_AS(MakeGraph(kVersions, 0), std::invalid_argument);
}

TEST_CASE("A failed switch leaves nothing behind and can be retried", "[runtime][submodel_manager]") {
  for (const int load_threads : {1, 4}) {
    CAPTURE(load_threads);
    const auto graph = MakeGraph(kFeatureOnly, load_threads);
    // x was built and y was not: x is removed along with the failure.
    REQUIRE_THROWS_WITH(graph->manager->SetFeatures({"x", "y"}), "Failure on switch to new submodel version");
    g_created.clear();
    REQUIRE_NOTHROW(graph->manager->SetFeatures({"x"}));
    REQUIRE(std::find(g_created.begin(), g_created.end(), "probe") != g_created.end());
  }
}

TEST_CASE("An exception inside a branch removes its version", "[runtime][submodel_manager]") {
  for (const int load_threads : {1, 4}) {
    CAPTURE(load_threads);
    const auto graph = MakeGraph(kNestedThrow, load_threads);
    REQUIRE_THROWS_AS(graph->manager->SetFeatures({"p", "q"}), std::out_of_range);
    REQUIRE_NOTHROW(graph->manager->SetFeatures({"p"}));
  }
}

TEST_CASE("A separate runtime is built in parallel as one by one", "[runtime][submodel_manager]") {
  std::vector<std::vector<std::string>> created;
  for (const int load_threads : {1, 4}) {
    const auto graph = MakeGraph(kVersions, load_threads);
    g_created.clear();
    g_max_loading = 0;
    g_wait_for_peer = load_threads > 1;
    REQUIRE(graph->manager->CreateRuntime("core") != nullptr);
    g_wait_for_peer = false;
    REQUIRE((load_threads > 1 ? g_max_loading > 1 : g_max_loading == 1));
    std::sort(g_created.begin(), g_created.end());
    created.push_back(g_created);
  }
  REQUIRE(created[0] == created[1]);
}

namespace {

// The whole UpdateAction, inactive versions included.
bool SameAction(const UpdateAction& lhs, const UpdateAction& rhs) {
  return lhs == rhs && lhs.inactive_versions == rhs.inactive_versions;
}

bool Consumed(const UpdateAction& action, const std::string& version) {
  return std::any_of(action.consumed_versions.begin(), action.consumed_versions.end(), [&](const auto& slot) {
    return slot.second == version;
  });
}

// A feature switches p to p2, and both submodels of p2 come only by features and neither is built: p stays on p1;
// s next to it switches to s2.
const std::map<std::string, Version> kFailingPair{
  {"core", {.children = {{"p", "p1"}, {"s", "s1"}}, .features = {{"p", "p2"}, {"s", "s2"}}, .probe = "core_probe"}},
  {"p1", {}},
  {"p2", {.children = {{"x", ""}, {"y", ""}}, .features = {{"x", "x_bad"}, {"y", "y_bad"}}}},
  {"x_bad", {.node = "parallel_load_fault"}},
  {"y_bad", {.node = "parallel_load_fault"}},
  {"s1", {}},
  {"s2", {}},
};

// With feature y a submodel needs a version whose node throws something that is not a std::exception: directly
// under the core or under p.
const std::map<std::string, Version> kAbort{
  {"core", {.children = {{"x", ""}, {"y", ""}, {"p", ""}}, .features = {{"x", "x"}, {"y", "y_abort"}, {"p", "p"}}}},
  {"x", {}},
  {"y_abort", {.probe = "abort", .node = "parallel_load_fault"}},
  {"p", {.children = {{"c", "c"}, {"q", ""}}, .features = {{"q", "q_abort"}}}},
  {"c", {}},
  {"q_abort", {.probe = "abort", .node = "parallel_load_fault"}},
};

struct PairRun {
  UpdateAction action;
  int faults_started = 0;
};

PairRun LoadFailingPair(int load_threads) {
  const auto graph = MakeGraph(kFailingPair, load_threads);
  g_faults_started = 0;
  g_faults_meet = load_threads > 1;
  PairRun run;
  run.action = graph->manager->SetFeatures({"p", "s", "x", "y"});
  g_faults_meet = false;
  run.faults_started = g_faults_started;
  return run;
}

// Adds feature a as soon as it sees the first update: a nested update from a hook, as with a custom hotword.
class FeatureOnFirstUpdate : public RuntimeCapabilityBase {
public:
  using RuntimeCapabilityBase::RuntimeCapabilityBase;

  void OnAfterStateUpdated(const UpdateAction& /*action*/) override {
    if (!added_) {
      added_ = true;
      AddFeature("a");
    }
  }

private:
  bool added_ = false;
};

} // namespace

TEST_CASE("Failures of siblings a loop would not reach are not reported", "[runtime][submodel_manager]") {
  const auto sequential = LoadFailingPair(1);
  const auto parallel = LoadFailingPair(4);

  // One by one: after the first one that is not built the second is not tried; in parallel both are tried.
  REQUIRE(sequential.faults_started == 1);
  REQUIRE(parallel.faults_started == 2);

  REQUIRE(Consumed(sequential.action, "p1"));
  REQUIRE(Consumed(sequential.action, "s2"));
  REQUIRE_FALSE(Consumed(sequential.action, "p2"));
  // The switch of p failed, p1 stays, p2 is inactive; of x_bad and y_bad only the one a loop would have reached.
  REQUIRE(sequential.action.required_versions.empty());
  REQUIRE(sequential.action.inactive_versions.size() == 2);
  REQUIRE(sequential.action.inactive_versions.count("p2") == 1);
  REQUIRE(sequential.action.inactive_versions.count("x_bad") + sequential.action.inactive_versions.count("y_bad") == 1);
  REQUIRE(SameAction(parallel.action, sequential.action));
}

TEST_CASE("Any number of load threads gives the result of one", "[runtime][submodel_manager]") {
  const auto sequential = Load(1);
  for (const int load_threads : {2, 3, 16}) {
    for (int attempt = 0; attempt < 3; ++attempt) {
      CAPTURE(load_threads, attempt);
      const auto parallel = Load(load_threads);
      REQUIRE(SameAction(parallel.initial, sequential.initial));
      REQUIRE(SameAction(parallel.switched, sequential.switched));
      REQUIRE(SameAction(parallel.broken_feature, sequential.broken_feature));
      REQUIRE(parallel.broken_feature.inactive_versions.count("d_broken") == 1);
    }
  }
}

TEST_CASE("A deferred part without data loads in parallel as one by one", "[runtime][submodel_manager]") {
  // a2 is built next to b_late, whose part is not on disk yet; once the data arrives b_late is picked up.
  const std::map<std::string, Version> versions{
    {"core",
     {.children = {{"a", "a1"}, {"b", "b1"}}, .features = {{"a", "a2"}, {"b", "b_late"}}, .probe = "core_probe"}},
    {"a1", {}},
    {"a2", {}},
    {"b1", {}},
  };
  std::vector<std::pair<UpdateAction, UpdateAction>> runs;
  for (const int load_threads : {1, 4}) {
    CAPTURE(load_threads);
    TempDir dir;
    std::vector<RuntimePart> parts;
    for (const auto& [name, version] : versions) {
      dir.Write(name + "/config.yaml", VersionConfig(name, version, load_threads));
      if (name != "core") {
        parts.push_back(
          {.id = name, .data = ModelFileData{(dir.path / name).string()}, .versions = {{name, {"config.yaml"}}}}
        );
      }
    }
    parts.push_back({
      .id = "late",
      .data = ModelFileData{(dir.path / "late").string()},
      .versions = {{"b_late", {"config.yaml"}}},
      .deferred = true,
    });
    RuntimeManager manager(
      {.id = "core",
       .data = ModelFileData{(dir.path / "core").string()},
       .versions = {{CORE_VERSION_NAME, {"config.yaml"}}}},
      parts
    );

    const auto refused = manager.SetFeatures({"a", "b"});
    REQUIRE(Consumed(refused, "a2"));
    REQUIRE(Consumed(refused, "b1"));
    REQUIRE(refused.required_versions == std::unordered_set<std::string>{"b_late"});
    REQUIRE(refused.inactive_versions == std::unordered_set<std::string>{"b_late"});

    dir.Write("late/config.yaml", VersionConfig("b_late", {}, load_threads));
    const auto picked_up = manager.SetFeatures({"a", "b"});
    REQUIRE(Consumed(picked_up, "a2"));
    REQUIRE(Consumed(picked_up, "b_late"));
    REQUIRE(picked_up.required_versions.empty());
    REQUIRE(picked_up.inactive_versions.empty());
    runs.emplace_back(refused, picked_up);
  }
  REQUIRE(SameAction(runs[1].first, runs[0].first));
  REQUIRE(SameAction(runs[1].second, runs[0].second));
}

TEST_CASE("A capability hook updates the state again on top of a parallel load", "[runtime][submodel_manager]") {
  std::vector<std::unordered_map<std::string, std::string>> consumed;
  for (const int load_threads : {1, 4}) {
    CAPTURE(load_threads);
    g_wait_for_peer = load_threads > 1;
    const auto graph = MakeGraph(kVersions, load_threads);
    // The stub of a disabled module registers an empty capability: nothing changes.
    graph->manager->RegisterCapability(std::shared_ptr<FeatureOnFirstUpdate>{});
    REQUIRE_FALSE(graph->manager->HasCapability<FeatureOnFirstUpdate>());
    graph->manager->RegisterCapability(std::make_shared<FeatureOnFirstUpdate>(*graph->manager));
    // b2 loads in the client's update, a2 in the nested one from the hook.
    REQUIRE_NOTHROW(graph->manager->SetFeatures({"b"}));
    g_wait_for_peer = false;
    const auto state = graph->manager->Model()->CollectConsumedVersions();
    REQUIRE(state.size() >= 2);
    REQUIRE(Consumed({.consumed_versions = state}, "a2"));
    REQUIRE(Consumed({.consumed_versions = state}, "b2"));
    REQUIRE(graph->manager->Features() == std::unordered_set<std::string>{"b"});
    consumed.push_back(state);
  }
  REQUIRE(consumed[0] == consumed[1]);
}

TEST_CASE(
  "A foreign exception from a parallel branch reaches the caller and leaves nothing", "[runtime][submodel_manager]"
) {
  for (const int load_threads : {1, 4}) {
    for (const auto* features : {"y", "q"}) {
      CAPTURE(load_threads, features);
      const auto graph = MakeGraph(kAbort, load_threads);
      const std::unordered_set<std::string> broken = std::string(features) == "y"
        ? std::unordered_set<std::string>{"x", "y", "p"}
        : std::unordered_set<std::string>{"x", "p", "q"};
      REQUIRE_THROWS_AS(graph->manager->SetFeatures(broken), LoadAbort);
      // The versions built next to it are removed: a retry without the broken feature builds them again.
      g_created.clear();
      REQUIRE_NOTHROW(graph->manager->SetFeatures({"x", "p"}));
      REQUIRE(std::count(g_created.begin(), g_created.end(), "probe") == 3);
      const auto state = graph->manager->Model()->CollectConsumedVersions();
      REQUIRE(Consumed({.consumed_versions = state}, "x"));
      REQUIRE(Consumed({.consumed_versions = state}, "p"));
      REQUIRE(Consumed({.consumed_versions = state}, "c"));
    }
  }
}
