#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

using namespace giga_edge;

namespace {

struct DeferredLayout {
  std::filesystem::path root;

  DeferredLayout() {
    root = std::filesystem::temp_directory_path()
      / ("gigaedge_deferred_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "core");
    std::filesystem::create_directories(root / "good" / "model");
    std::ofstream(root / "core" / "config.yaml") << R"(
version: core
lib_version: '1.20'
num_threads: 0
nodes:
  - name: echo
    type: coalesce_text
    inputs:
      first: text
      second: text
    outputs:
      text: core_text
submodels:
  - id: choice
    inputs:
      input: text
    outputs:
      text: choice_text
default_submodels:
  choice:
    submodel_version: choice.good
features:
  - feature_ids: [later]
    submodel_id: choice
    submodel_version: choice.later
  - feature_ids: []
    submodel_id: choice
    submodel_version: choice.good
)";
    std::ofstream(root / "good" / "model" / "good.yaml") << Echo("choice.good");
  }
  ~DeferredLayout() {
    std::filesystem::remove_all(root);
  }

  static std::string Echo(const std::string& version) {
    return "version: " + version + R"(
lib_version: '1.20'
nodes:
  - name: echo
    type: coalesce_text
    inputs:
      first: input
      second: input
    outputs:
      text: text
)";
  }

  RuntimePart Core() const {
    return RuntimePart{
      .id = "core", .data = ModelFileData{(root / "core").string()}, .versions = {{CORE_VERSION_NAME, {"config.yaml"}}}
    };
  }
  RuntimePart Good() const {
    return RuntimePart{
      .id = "good",
      .data = ModelFileData{(root / "good" / "model").string()},
      .versions = {{"choice.good", {"good.yaml"}}}
    };
  }
  RuntimePart Later() const {
    return RuntimePart{
      .id = "later",
      .data = ModelFileData{(root / "later" / "model").string()},
      .versions = {{"choice.later", {"later.yaml"}}},
      .deferred = true,
    };
  }
  void DeliverLater() const {
    std::filesystem::create_directories(root / "later" / "model");
    std::ofstream(root / "later" / "model" / "later.yaml") << Echo("choice.later");
  }
};

} // namespace

TEST_CASE("runtime_manager: a deferred part is ready once its configs are readable", "[runtime_manager][deferred]") {
  DeferredLayout layout;
  RuntimeManager manager(layout.Core(), {layout.Good(), layout.Later()});
  CHECK(manager.IsPartReady("core"));
  CHECK(manager.IsPartReady("good"));
  CHECK_FALSE(manager.IsPartReady("absent"));
  CHECK(manager.HasPart("later"));
  CHECK_FALSE(manager.IsPartReady("later"));

  CHECK(manager.GetVersionPart("choice.good") == "good");
  CHECK(manager.GetVersionPart("choice.later") == "later");
  CHECK_FALSE(manager.GetVersionPart("choice.absent").has_value());

  std::filesystem::create_directories(layout.root / "later" / "model");
  std::ofstream(layout.root / "later" / "model" / "later.yaml") << "version: [";
  CHECK_FALSE(manager.IsPartReady("later"));

  layout.DeliverLater();
  CHECK(manager.IsPartReady("later"));
  (void)manager.RemoveRuntimeParts({"later"});
  CHECK_FALSE(manager.IsPartReady("later"));
  CHECK_FALSE(manager.GetVersionPart("choice.later").has_value());
}

TEST_CASE(
  "runtime_manager: a deferred part without data is reported as required and inactive", "[runtime_manager][deferred]"
) {
  DeferredLayout layout;
  RuntimeManager manager(layout.Core(), {layout.Good(), layout.Later()});
  const auto initial = manager.InitialUpdateState();
  REQUIRE(initial.inactive_versions.empty());
  REQUIRE(manager.Model());

  const auto refused = manager.SetFeatures({"later"});
  REQUIRE(refused.inactive_versions == std::unordered_set<std::string>{"choice.later"});
  REQUIRE(refused.required_versions == std::unordered_set<std::string>{"choice.later"});
  REQUIRE(refused.freed_parts.empty());
  REQUIRE(manager.Features() == std::unordered_set<std::string>{"later"});

  const auto restored = manager.SetFeatures({});
  REQUIRE(restored.inactive_versions.empty());
  REQUIRE(restored.required_versions.empty());

  layout.DeliverLater();
  const auto picked_up = manager.SetFeatures({"later"});
  REQUIRE(picked_up.inactive_versions.empty());
  REQUIRE(picked_up.required_versions.empty());
  REQUIRE(picked_up.consumed_versions.at("choice") == "choice.later");
}
