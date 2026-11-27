#include "giga_edge/lib/runtime_manager/feature_manager.h"

#include "giga_edge/lib/runtime/tests/runtime_test_utils/temp_dir.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

using namespace giga_edge;
using giga_edge::test::TempDir;

namespace {

void WriteConfig(const std::filesystem::path& dir, const std::string& body) {
  std::filesystem::create_directories(dir);
  std::ofstream out(dir / "config.yaml");
  if (!out) {
    throw std::runtime_error("failed to open " + (dir / "config.yaml").string());
  }
  out << body;
}

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

struct FeatureManagerFixture {
  TempDir tmp;
  RuntimeManager manager;
  FeatureManager& feature_manager;

  explicit FeatureManagerFixture(std::unordered_set<std::string> core_features = {})
    : manager(
        RuntimePart{
          .id = "core",
          .data = ModelFileData{CreateCorePath(tmp)},
          .versions = {{"core", {.config_path = "config.yaml"}}},
        },
        {},
        {},
        std::move(core_features)
      )
    , feature_manager(manager.GetFeatureManager()) {}

private:
  static std::string CreateCorePath(TempDir& tmp) {
    WriteConfig(tmp.path / "core", kCoreConfig);
    return (tmp.path / "core").string();
  }
};

} // namespace

TEST_CASE("FeatureManager constructor initializes client features and aggregated set", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0", "feature_client_1"});
  auto& feature_manager = fixture.feature_manager;

  REQUIRE(feature_manager.GetFeatures() == std::unordered_set<std::string>{"feature_client_0", "feature_client_1"});
  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CLIENT)
    == std::unordered_set<std::string>{"feature_client_0", "feature_client_1"}
  );
}

TEST_CASE("FeatureManager AddFeature adds to specific set and aggregates", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;

  feature_manager.AddFeature(fixture.manager, "feature_capability_0", FeatureSetType::CAPABILITY);

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CLIENT) == std::unordered_set<std::string>{"feature_client_0"});
  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY) == std::unordered_set<std::string>{"feature_capability_0"}
  );
  REQUIRE(feature_manager.GetFeatures() == std::unordered_set<std::string>{"feature_client_0", "feature_capability_0"});
}

TEST_CASE("FeatureManager RemoveFeature removes from specific set and aggregated", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CAPABILITY).size() == 1);

  feature_manager.RemoveFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CAPABILITY).empty());
  REQUIRE(feature_manager.GetFeatures() == std::unordered_set<std::string>{"feature_client_0"});
}

TEST_CASE("FeatureManager SetFeatures replaces entire set and rebuilds aggregated", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);
  feature_manager.SetFeatures(manager, {"feature_capability_1", "feature_capability_2"}, FeatureSetType::CAPABILITY);

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CLIENT) == std::unordered_set<std::string>{"feature_client_0"});
  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY)
    == std::unordered_set<std::string>{"feature_capability_1", "feature_capability_2"}
  );
  REQUIRE(
    feature_manager.GetFeatures()
    == std::unordered_set<std::string>{"feature_client_0", "feature_capability_1", "feature_capability_2"}
  );
}

TEST_CASE("FeatureManager AddFeature rejects feature already in another set", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;

  REQUIRE_THROWS_WITH(
    feature_manager.AddFeature(fixture.manager, "feature_client_0", FeatureSetType::CAPABILITY),
    Catch::Contains("Feature 'feature_client_0' of 'capability' feature set already exists in another feature set")
  );

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CLIENT) == std::unordered_set<std::string>{"feature_client_0"});
}

TEST_CASE("FeatureManager SetFeatures rejects features already in another set", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);

  REQUIRE_THROWS_WITH(
    feature_manager.SetFeatures(manager, {"feature_capability_0", "feature_client_1"}, FeatureSetType::CLIENT),
    Catch::Contains("Feature 'feature_capability_0' of 'client' feature set already exists in another feature set")
  );

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CLIENT) == std::unordered_set<std::string>{"feature_client_0"});
  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY) == std::unordered_set<std::string>{"feature_capability_0"}
  );
}

TEST_CASE("FeatureManager RemoveFeature is no-op for nonexistent feature", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);
  feature_manager.RemoveFeature(manager, "feature_capability_99", FeatureSetType::CAPABILITY);

  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY) == std::unordered_set<std::string>{"feature_capability_0"}
  );
}

TEST_CASE("FeatureManager RemoveFeature via a foreign set keeps the feature", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);
  feature_manager.RemoveFeature(manager, "feature_capability_0", FeatureSetType::CLIENT);

  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY) == std::unordered_set<std::string>{"feature_capability_0"}
  );
  REQUIRE(feature_manager.GetFeatures() == std::unordered_set<std::string>{"feature_client_0", "feature_capability_0"});
}

TEST_CASE("FeatureManager SetFeatures replaces client features", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;

  feature_manager.AddFeature(fixture.manager, "feature_capability_0", FeatureSetType::CAPABILITY);
  feature_manager.SetFeatures({"feature_client_1", "feature_client_2"}, FeatureSetType::CLIENT);

  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CLIENT)
    == std::unordered_set<std::string>{"feature_client_1", "feature_client_2"}
  );
  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY) == std::unordered_set<std::string>{"feature_capability_0"}
  );
  REQUIRE(
    feature_manager.GetFeatures()
    == std::unordered_set<std::string>{"feature_client_1", "feature_client_2", "feature_capability_0"}
  );
}

TEST_CASE("FeatureManager AddFeature to sets works correctly", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);
  feature_manager.AddFeature(manager, "feature_client_2", FeatureSetType::CLIENT);

  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CLIENT)
    == std::unordered_set<std::string>{"feature_client_0", "feature_client_2"}
  );
  REQUIRE(
    feature_manager.GetFeatures(FeatureSetType::CAPABILITY) == std::unordered_set<std::string>{"feature_capability_0"}
  );
  REQUIRE(
    feature_manager.GetFeatures()
    == std::unordered_set<std::string>{"feature_client_0", "feature_client_2", "feature_capability_0"}
  );
}

TEST_CASE("FeatureManager AddFeature rejects duplicate within same set is no-op", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;

  feature_manager.AddFeature(fixture.manager, "feature_client_0", FeatureSetType::CLIENT);

  REQUIRE(feature_manager.GetFeatures(FeatureSetType::CLIENT) == std::unordered_set<std::string>{"feature_client_0"});
  REQUIRE(feature_manager.GetFeatures() == std::unordered_set<std::string>{"feature_client_0"});
}

TEST_CASE("FeatureManager aggregated set reflects all types", "[runtime][feature_manager]") {
  FeatureManagerFixture fixture({"feature_client_0"});
  auto& feature_manager = fixture.feature_manager;
  auto& manager = fixture.manager;

  feature_manager.AddFeature(manager, "feature_client_1", FeatureSetType::CLIENT);
  feature_manager.AddFeature(manager, "feature_capability_0", FeatureSetType::CAPABILITY);

  REQUIRE(
    feature_manager.GetFeatures()
    == std::unordered_set<std::string>{"feature_client_0", "feature_client_1", "feature_capability_0"}
  );

  feature_manager.RemoveFeature(manager, "feature_client_1", FeatureSetType::CLIENT);

  REQUIRE(feature_manager.GetFeatures() == std::unordered_set<std::string>{"feature_client_0", "feature_capability_0"});
}
