#include "giga_edge/lib/environment/environment.h"
#include "giga_edge/lib/runtime/tests/runtime_test_utils/temp_dir.h"
#include "giga_edge/lib/runtime_manager/capability.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

using namespace giga_edge;
using giga_edge::test::TempDir;

namespace {

class ITestCapability : public RuntimeCapabilityBase {
public:
  explicit ITestCapability(RuntimeManager& manager)
    : RuntimeCapabilityBase(manager) {}
  ~ITestCapability() override = default;
};

class ManagerCallback final : public ITestCapability {
public:
  explicit ManagerCallback(RuntimeManager& manager)
    : ITestCapability(manager)
    , manager_(manager) {}

protected:
  void OnBeforePartsRemoved(const std::vector<std::string>& part_ids) override {
    removed_parts.push_back(part_ids);
    parts_present_before_removal.push_back(manager_.HasPart(part_ids.front()));
  }

  void OnAfterStateUpdated(const UpdateAction& action) override {
    update_actions.push_back(action);
    if (add_feature_once) {
      add_feature_once = false;
      manager_.AddFeature("added-from-callback");
    }
  }

public:
  std::vector<std::vector<std::string>> removed_parts;
  std::vector<bool> parts_present_before_removal;
  std::vector<UpdateAction> update_actions;
  bool add_feature_once = false;

private:
  RuntimeManager& manager_;
};

void WriteConfig(const std::filesystem::path& dir, const std::string& body) {
  std::filesystem::create_directories(dir);
  std::ofstream out(dir / "config.yaml");
  if (!out) {
    throw std::runtime_error("failed to open " + (dir / "config.yaml").string());
  }
  out << body;
}

// Minimal core: 1 thread, 1 trivial pass-through node, no submodels, no features.
constexpr const char* kCoreConfig = R"(num_threads: 1
version: test
lib_version: 1.18

nodes:
  - type: logical_expression
    name: pass_through
    inputs:
      input: input
    outputs:
      result: output
    params:
      expression: 'input'
)";

// Minimal non-core part: same shape, no submodels.
constexpr const char* kPartConfig = R"(nodes:
  - type: logical_expression
    name: pass_through
    inputs:
      input: input
    outputs:
      result: output
    params:
      expression: 'input'
)";

[[maybe_unused]] RuntimePart MakeCore(const TempDir& tmp) {
  WriteConfig(tmp.path / "core", kCoreConfig);
  return RuntimePart{
    .id = "core",
    .data = ModelFileData{(tmp.path / "core").string()},
    .versions = {{"core", {.config_path = "config.yaml"}}},
  };
}

// permanent=true so the part isn't garbage-collected by UpdateState's free_unused
// logic — SetState must still remove it explicitly when it is absent from the new state.
[[maybe_unused]] RuntimePart MakePart(const TempDir& tmp, const std::string& id) {
  WriteConfig(tmp.path / id, kPartConfig);
  return RuntimePart{
    .id = id,
    .data = ModelFileData{(tmp.path / id).string()},
    .versions = {{id, {.config_path = "config.yaml"}}},
    .permanent = true,
  };
}

RuntimePart MakeStandaloneRuntime(const TempDir& tmp, const std::string& id) {
  WriteConfig(tmp.path / id, kCoreConfig);
  return RuntimePart{
    .id = id,
    .data = ModelFileData{(tmp.path / id).string()},
    .versions = {{id, {.config_path = "config.yaml"}}},
  };
}

RuntimePart MakeStandaloneRuntimeWithChild(const TempDir& tmp) {
  WriteConfig(tmp.path / "updater", R"(num_threads: 1
version: updater
lib_version: 1.18

submodels:
  - id: child
    inputs: { input: input }
    outputs: { output: child_output }

default_submodels:
  child: child

nodes:
  - type: logical_expression
    name: pass_through
    inputs:
      input: child_output
    outputs:
      result: output
    params:
      expression: 'input'
)");
  return RuntimePart{
    .id = "updater",
    .data = ModelFileData{(tmp.path / "updater").string()},
    .versions = {{"updater", {.config_path = "config.yaml"}}},
  };
}

RuntimePart MakeStandaloneChild(const TempDir& tmp) {
  WriteConfig(tmp.path / "child", R"(submodels:
  - id: leaf
    inputs: { input: input }
    outputs: { output: leaf_output }

default_submodels:
  leaf: leaf

nodes:
  - type: logical_expression
    name: pass_through
    inputs:
      input: leaf_output
    outputs:
      result: output
    params:
      expression: 'input'
)");
  return RuntimePart{
    .id = "child",
    .data = ModelFileData{(tmp.path / "child").string()},
    .versions = {{"child", {.config_path = "config.yaml"}}},
  };
}

RuntimePart MakeStandaloneLeaf(const TempDir& tmp) {
  WriteConfig(tmp.path / "leaf", kPartConfig);
  return RuntimePart{
    .id = "leaf",
    .data = ModelFileData{(tmp.path / "leaf").string()},
    .versions = {{"leaf", {.config_path = "config.yaml"}}},
  };
}

} // namespace

TEST_CASE("Constructor applies initial features", "[runtime][submodel_manager][setstate]") {
  TempDir tmp;
  auto core = MakeCore(tmp);

  RuntimeManager manager(core, /*parts=*/{}, /*environments=*/{}, /*features=*/{"f0"});

  REQUIRE(manager.Features() == std::unordered_set<std::string>{"f0"});
}

TEST_CASE("CreateRuntime creates an independent graph from a part version", "[runtime][submodel_manager][setstate]") {
  TempDir tmp;
  auto core = MakeCore(tmp);
  auto updater = MakeStandaloneRuntime(tmp, "updater");
  auto environment = std::make_shared<EmptyEnvironment>();
  RuntimeManager manager(core, {updater}, {{"test", environment}});

  auto model = manager.CreateRuntime("updater");
  REQUIRE(model->GetEnvironment("test") == nullptr);

  std::promise<bool> result_promise;
  auto result = result_promise.get_future();
  model->Subscribe<bool>({}, "output", [&result_promise](bool value, const std::any&) {
    result_promise.set_value(value);
  });

  model->Push({}, "input", true, {});

  REQUIRE(result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  REQUIRE(result.get());
  REQUIRE(manager.Model()->HasOutput({}, "output"));
}

TEST_CASE("CreateRuntime adds recursive submodels", "[runtime][submodel_manager][setstate]") {
  TempDir tmp;
  auto core = MakeCore(tmp);
  auto updater = MakeStandaloneRuntimeWithChild(tmp);
  auto child = MakeStandaloneChild(tmp);
  auto leaf = MakeStandaloneLeaf(tmp);
  RuntimeManager manager(core, {updater, child, leaf});

  auto model = manager.CreateRuntime("updater");
  const SubmodelVersionChain leaf_chain{
    .ids_chain = {{.id = "child", .version = "child"}, {.id = "leaf", .version = "leaf"}},
  };
  REQUIRE(model->HasOutput(leaf_chain, "output"));

  std::promise<bool> result_promise;
  auto result = result_promise.get_future();
  model->Subscribe<bool>({}, "output", [&result_promise](bool value, const std::any&) {
    result_promise.set_value(value);
  });
  model->Push({}, "input", true, {});

  REQUIRE(result.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
  REQUIRE(result.get());
}

TEST_CASE(
  "SetState preserves data_reader identity for existing parts (including core)", "[runtime][submodel_manager][setstate]"
) {
  TempDir tmp;
  auto core = MakeCore(tmp);
  auto a = MakePart(tmp, "A");

  RuntimeManager manager(core, {a});

  ILazyDataReaderPtr reader_core_before = manager.GetDataReader(core.id);
  ILazyDataReaderPtr reader_a_before = manager.GetDataReader("A");
  REQUIRE(reader_core_before != nullptr);
  REQUIRE(reader_a_before != nullptr);

  // Re-pass core and A (including a deliberately-mutated A payload) to confirm
  // the live RuntimePartStatus is preserved rather than rebuilt.
  TempDir tmp2;
  auto a_mutated = MakePart(tmp2, "A"); // different on-disk path
  auto core_clone = core;               // same id, same content, different RuntimePart instance

  REQUIRE_NOTHROW(manager.SetState({core_clone, a_mutated}, {}));

  ILazyDataReaderPtr reader_core_after = manager.GetDataReader(core.id);
  ILazyDataReaderPtr reader_a_after = manager.GetDataReader("A");

  REQUIRE(reader_core_after.get() == reader_core_before.get());
  REQUIRE(reader_a_after.get() == reader_a_before.get());
}

TEST_CASE("SetState removes parts absent from new list", "[runtime][submodel_manager][setstate]") {
  TempDir tmp;
  auto core = MakeCore(tmp);
  auto a = MakePart(tmp, "A");
  auto b = MakePart(tmp, "B");

  RuntimeManager manager(core, {a, b});
  REQUIRE(manager.HasPart("A"));
  REQUIRE(manager.HasPart("B"));

  manager.SetState({core, a}, {});

  REQUIRE(manager.HasPart(core.id));
  REQUIRE(manager.HasPart("A"));
  REQUIRE_FALSE(manager.HasPart("B"));
}

TEST_CASE("SetState adds genuinely new parts", "[runtime][submodel_manager][setstate]") {
  TempDir tmp;
  auto core = MakeCore(tmp);
  auto a = MakePart(tmp, "A");

  RuntimeManager manager(core, {a});
  ILazyDataReaderPtr reader_a_before = manager.GetDataReader("A");
  REQUIRE_FALSE(manager.HasPart("B"));

  auto b = MakePart(tmp, "B");
  manager.SetState({core, a, b}, {});

  REQUIRE(manager.HasPart("B"));
  REQUIRE(manager.GetDataReader("A").get() == reader_a_before.get());
}

TEST_CASE(
  "SetState replaces public features and keeps other ones", "[runtime][submodel_manager][setstate][feature_manager]"
) {
  TempDir tmp;
  auto core = MakeCore(tmp);

  RuntimeManager manager(core, {}, {}, {"feature_core_0"});
  auto& feature_manager = manager.GetFeatureManager();
  feature_manager.AddFeature(manager, "feature_capability_0", giga_edge::FeatureSetType::CAPABILITY);
  REQUIRE(manager.Features() == std::unordered_set<std::string>{"feature_core_0"});
  REQUIRE(
    feature_manager.GetFeatures(giga_edge::FeatureSetType::CLIENT) == std::unordered_set<std::string>{"feature_core_0"}
  );
  REQUIRE(
    feature_manager.GetFeatures(giga_edge::FeatureSetType::CAPABILITY)
    == std::unordered_set<std::string>{"feature_capability_0"}
  );

  manager.SetState({core}, {"feature_core_1", "feature_core_2"});

  REQUIRE(manager.Features() == std::unordered_set<std::string>{"feature_core_1", "feature_core_2"});
  REQUIRE(
    feature_manager.GetFeatures(giga_edge::FeatureSetType::CLIENT)
    == std::unordered_set<std::string>{"feature_core_1", "feature_core_2"}
  );
  REQUIRE(
    feature_manager.GetFeatures(giga_edge::FeatureSetType::CAPABILITY)
    == std::unordered_set<std::string>{"feature_capability_0"}
  );
}

TEST_CASE(
  "Runtime manager callbacks bracket part removal and observe every state update",
  "[runtime][submodel_manager][callbacks]"
) {
  TempDir tmp;
  auto core = MakeCore(tmp);
  auto unused = MakePart(tmp, "unused");
  unused.permanent = false;

  RuntimeManager manager(core, {unused});
  auto callback = std::make_shared<ManagerCallback>(manager);
  manager.RegisterCapability<ITestCapability>(callback);

  manager.InitialUpdateState();
  REQUIRE(callback->removed_parts == std::vector<std::vector<std::string>>{{"unused"}});
  REQUIRE(callback->parts_present_before_removal == std::vector<bool>{true});
  REQUIRE_FALSE(manager.HasPart("unused"));
  REQUIRE(callback->update_actions.size() == 1);
  REQUIRE(callback->update_actions.back().freed_parts == std::unordered_set<std::string>{"unused"});

  auto explicit_part = MakePart(tmp, "explicit");
  manager.AddRuntimeParts({explicit_part});
  manager.RemoveRuntimeParts({"explicit"});

  REQUIRE(callback->removed_parts == std::vector<std::vector<std::string>>{{"unused"}, {"explicit"}});
  REQUIRE(callback->parts_present_before_removal == std::vector<bool>{true, true});
  REQUIRE_FALSE(manager.HasPart("explicit"));
  REQUIRE(callback->update_actions.size() == 3);
}

TEST_CASE("Runtime manager skips an absent capability", "[runtime][submodel_manager][callbacks]") {
  TempDir tmp;
  auto core = MakeCore(tmp);

  RuntimeManager manager(core);
  manager.RegisterCapability<ITestCapability>(nullptr);

  REQUIRE_FALSE(manager.HasCapability<ITestCapability>());
  REQUIRE(manager.GetCapability<ITestCapability>() == nullptr);
  REQUIRE_NOTHROW(manager.InitialUpdateState());
  REQUIRE_NOTHROW(manager.SetState({core}, {}));
}

TEST_CASE("Runtime manager notifies about SetState once", "[runtime][submodel_manager][callbacks]") {
  TempDir tmp;
  auto core = MakeCore(tmp);

  RuntimeManager manager(core);
  auto callback = std::make_shared<ManagerCallback>(manager);
  manager.RegisterCapability<ITestCapability>(callback);

  manager.SetState({core}, {});

  REQUIRE(callback->update_actions.size() == 1);
}

TEST_CASE("Runtime manager lifecycle callback can change state", "[runtime][submodel_manager][callbacks]") {
  TempDir tmp;
  auto core = MakeCore(tmp);

  RuntimeManager manager(core);
  auto callback = std::make_shared<ManagerCallback>(manager);
  callback->add_feature_once = true;
  manager.RegisterCapability<ITestCapability>(callback);

  manager.InitialUpdateState();

  REQUIRE(manager.Features() == std::unordered_set<std::string>{"added-from-callback"});
  REQUIRE(callback->update_actions.size() == 2);
}
