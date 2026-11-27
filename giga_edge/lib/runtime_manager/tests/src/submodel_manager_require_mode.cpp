#include "matchers.h"

#include "giga_edge/lib/common/str_utils.h"
#include "giga_edge/lib/enum/enum.h"
#include "giga_edge/lib/runtime/tests/runtime_test_utils/temp_dir.h"
#include "giga_edge/lib/runtime_manager/runtime_manager.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace giga_edge;
using giga_edge::test::TempDir;

namespace {

void WriteConfig(const std::filesystem::path& dir, const std::string& contents) {
  std::filesystem::create_directories(dir);
  std::ofstream out(dir / "config.yaml");
  if (!out) {
    throw std::runtime_error("failed to open " + (dir / "config.yaml").string());
  }
  out << contents;
}

RuntimePart MakePart(const std::filesystem::path& base, std::string name) {
  std::filesystem::path part_dir = base / name;
  return RuntimePart{
    .id = name,
    .data = ModelFileData{part_dir.string()},
    .versions = {{name, {.config_path = "config.yaml"}}},
  };
}

std::string DefaultEntry(const std::string& slot_id, const DefaultSubmodel& ds) {
  if (ds.require_mode == SubmodelRequireMode::NONE) {
    return "  " + slot_id + ": " + ds.submodel_version + "\n";
  }
  std::stringstream out;
  out << "  " + slot_id + ":\n"
      << "    submodel_version: " + ds.submodel_version + "\n"
      << "    require_mode: " + std::string(EnumToString(ds.require_mode)) + "\n";
  return out.str();
}

std::string MakeVersionConfig(
  const std::unordered_map<std::string, DefaultSubmodel>& defaults = {},
  const std::vector<SubmodelFeature>& features = {},
  bool is_core = false
) {
  std::stringstream out;
  out << "version: test\n";

  if (is_core) {
    out << "num_threads: 1\n"
        << "lib_version: 1.18\n"
        << "\n";
  }

  std::vector<std::string> variables = {"input"};

  auto emit_submodel = [&](const std::string& submodel_name) {
    out << "  - id: " + submodel_name + "\n"
        << "    inputs:  { input: input }\n"
        << "    outputs: { output: " + submodel_name + "_out }\n";
    variables.push_back(submodel_name + "_out");
  };

  if (!defaults.empty() || !features.empty()) {
    out << "submodels:\n";
    std::unordered_set<std::string> emitted;
    for (const auto& [slot_id, _] : defaults) {
      if (emitted.insert(slot_id).second) {
        emit_submodel(slot_id);
      }
    }
    for (const auto& f : features) {
      if (emitted.insert(f.submodel_id).second) {
        emit_submodel(f.submodel_id);
      }
    }
    out << "\n";
  }

  if (!defaults.empty()) {
    out << "default_submodels:\n";
    for (const auto& [slot_id, ds] : defaults) {
      out << DefaultEntry(slot_id, ds);
    }
    out << "\n";
  }

  if (!features.empty()) {
    out << "features:\n";
    for (const auto& f : features) {
      out << "  - feature_ids: [" << JoinStrings(f.feature_ids, ", ") << "]\n"
          << "    submodel_id: " << f.submodel_id << "\n"
          << "    submodel_version: " << f.submodel_version << "\n"
          << "    require_mode: " << std::string(EnumToString(f.require_mode)) << "\n";
    }
  }

  out << "nodes:\n"
      << "  - type: logical_expression\n"
      << "    name: pass_through\n"
      << "    inputs:\n";
  for (const auto& variable : variables) {
    out << "      " << variable << ": " << variable << "\n";
  }
  out << "    outputs:\n"
      << "      result: output\n"
      << "    params:\n"
      << "      expression: '" << JoinStrings(variables, " && ") << "'\n";

  return out.str();
}

struct Fixture {
  TempDir tmp;
  int counter = 0;

  std::string NextName() {
    return "v" + std::to_string(++counter);
  }

  RuntimePart CreateSubmodelVersion(
    const std::string& name,
    const std::unordered_map<std::string, DefaultSubmodel>& defaults = {},
    const std::vector<SubmodelFeature>& features = {}
  ) {
    WriteConfig(tmp.path / name, MakeVersionConfig(defaults, features, name == "core"));
    return MakePart(tmp.path, name);
  }

  RuntimePart CreateSubmodelVersion(
    const std::string& name,
    const std::vector<std::pair<std::string, DefaultSubmodel>>& defaults,
    const std::vector<SubmodelFeature>& features
  ) {
    return CreateSubmodelVersion(
      name, std::unordered_map<std::string, DefaultSubmodel>{defaults.begin(), defaults.end()}, features
    );
  }

  std::pair<std::string, DefaultSubmodel> CreateDefaultSubmodel(
    const std::string& name, SubmodelRequireMode require_mode = SubmodelRequireMode::NONE
  ) {
    return {name, {.submodel_version = name + "_default", .require_mode = require_mode}};
  }

  SubmodelFeature CreateSubmodelFeature(
    const std::string& name,
    SubmodelRequireMode require_mode = SubmodelRequireMode::NONE,
    const std::string& feature_suffix = "_feature"
  ) {
    return {
      .feature_ids = {name + feature_suffix},
      .submodel_id = name,
      .submodel_version = name + feature_suffix,
      .require_mode = require_mode
    };
  }
};

} // namespace

TEST_CASE_METHOD(Fixture, "require_mode, immediate", "[runtime][submodel_manager][require_mode]") {
  // core
  //   -> a_default (+)
  //     -> a1_default(+)
  //     -> a1_feature(-)
  //   -> a_feature(-, immediate)
  //     -> a2_default(-, immediate)
  //       -> a21_default(-)
  //     -> a2_feature_1(-, selective)
  //       -> a22_default(-)
  //     -> a2_feature_2(-, recursive)
  //       -> a23_default(-)
  auto core = CreateSubmodelVersion(
    "core",
    std::vector{CreateDefaultSubmodel("a")},
    std::vector{CreateSubmodelFeature("a", SubmodelRequireMode::IMMEDIATE)}
  );

  auto a_default = CreateSubmodelVersion(
    "a_default", std::vector{CreateDefaultSubmodel("a1")}, std::vector{CreateSubmodelFeature("a1")}
  );

  auto a1_default = CreateSubmodelVersion("a1_default");
  auto a1_feature = CreateSubmodelVersion("a1_feature");

  auto a_feature = CreateSubmodelVersion(
    "a_feature",
    std::vector{CreateDefaultSubmodel("a2", SubmodelRequireMode::IMMEDIATE)},
    std::vector{
      CreateSubmodelFeature("a2", SubmodelRequireMode::SELECTIVE, "_feature_1"),
      CreateSubmodelFeature("a2", SubmodelRequireMode::RECURSIVE, "_feature_2"),
    }
  );

  auto a2_default = CreateSubmodelVersion("a2_default", std::vector{CreateDefaultSubmodel("a21")}, {});
  auto a21_default = CreateSubmodelVersion("a21_default");

  auto a2_feature_1 = CreateSubmodelVersion("a2_feature_1", std::vector{CreateDefaultSubmodel("a22")}, {});
  auto a22_default = CreateSubmodelVersion("a22_default");

  auto a2_feature_2 = CreateSubmodelVersion("a2_feature_2", std::vector{CreateDefaultSubmodel("a23")}, {});
  auto a23_default = CreateSubmodelVersion("a23_default");

  SECTION("Add part") {
    RuntimeManager manager(core, {a_default, a1_default});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a_feature});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.RemoveRuntimeParts({"a_feature"});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );
  }

  SECTION("All parts present") {
    RuntimeManager manager(
      core,
      {a_default,
       a1_default,
       a1_feature,
       a_feature,
       a2_default,
       a21_default,
       a2_feature_1,
       a22_default,
       a2_feature_2,
       a23_default}
    );
    auto action = manager.InitialUpdateState();
    REQUIRE(action == UpdateAction{
      .freed_parts = {
        "a1_feature", "a2_default", "a21_default", "a2_feature_1", "a22_default", "a2_feature_2", "a23_default",
      },
      .required_versions = {}, .consumed_versions = action.consumed_versions,
    });
  }
}

TEST_CASE_METHOD(Fixture, "require_mode, recursive", "[runtime][submodel_manager][require_mode]") {
  // core
  //   -> a_default (+)
  //     -> a1_default(+)
  //     -> a1_feature(-)
  //   -> a_feature(-, recursive)
  //     -> a2_default(-, immediate)
  //       -> a21_default(-)
  //     -> a2_feature_1(-, selective)
  //       -> a22_default(-)
  //     -> a2_feature_2(-, recursive)
  //       -> a23_default(-)
  auto core = CreateSubmodelVersion(
    "core",
    std::vector{CreateDefaultSubmodel("a")},
    std::vector{CreateSubmodelFeature("a", SubmodelRequireMode::RECURSIVE)}
  );

  auto a_default = CreateSubmodelVersion(
    "a_default", std::vector{CreateDefaultSubmodel("a1")}, std::vector{CreateSubmodelFeature("a1")}
  );

  auto a1_default = CreateSubmodelVersion("a1_default");
  auto a1_feature = CreateSubmodelVersion("a1_feature");

  auto a_feature = CreateSubmodelVersion(
    "a_feature",
    std::vector{CreateDefaultSubmodel("a2", SubmodelRequireMode::IMMEDIATE)},
    std::vector{
      CreateSubmodelFeature("a2", SubmodelRequireMode::SELECTIVE, "_feature_1"),
      CreateSubmodelFeature("a2", SubmodelRequireMode::RECURSIVE, "_feature_2"),
    }
  );

  auto a2_default = CreateSubmodelVersion("a2_default", std::vector{CreateDefaultSubmodel("a21")}, {});
  auto a21_default = CreateSubmodelVersion("a21_default");

  auto a2_feature_1 = CreateSubmodelVersion("a2_feature_1", std::vector{CreateDefaultSubmodel("a22")}, {});
  auto a22_default = CreateSubmodelVersion("a22_default");

  auto a2_feature_2 = CreateSubmodelVersion("a2_feature_2", std::vector{CreateDefaultSubmodel("a23")}, {});
  auto a23_default = CreateSubmodelVersion("a23_default");

  SECTION("Add part") {
    RuntimeManager manager(core, {a_default, a1_default});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a_feature});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a2_default", "a2_feature_1", "a2_feature_2"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a2_default, a2_feature_1, a2_feature_2});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a21_default", "a22_default", "a23_default"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a21_default, a22_default, a23_default});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.RemoveRuntimeParts({"a_feature"});
    REQUIRE(action == UpdateAction{
      .freed_parts = {
        "a2_default", "a21_default",
        "a2_feature_1", "a22_default",
        "a2_feature_2", "a23_default",
      },
      .required_versions = {"a_feature"}, .consumed_versions = action.consumed_versions,
    });
  }

  SECTION("Pre existing parts") {
    RuntimeManager manager(
      core,
      {a_default,
       a1_default,
       a1_feature,
       a_feature,
       a2_default,
       a21_default,
       a2_feature_1,
       a22_default,
       a2_feature_2,
       a23_default}
    );
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {"a1_feature"},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );
  }
}

TEST_CASE_METHOD(Fixture, "require_mode, selective", "[runtime][submodel_manager][require_mode]") {
  // core
  //   -> a_default (+)
  //     -> a1_default(+)
  //     -> a1_feature(-)
  //   -> a_feature(-, selective)
  //     -> a2_default(-, immediate)
  //       -> a21_default(-)
  //         -> a211_default(-)
  //     -> a2_feature_1(-, selective)
  //       -> a22_default(-, selective)
  //         -> a221_default(-)
  //     -> a2_feature_2(-, recursive)
  //       -> a23_default(-)
  //         -> a231_default(-)
  auto core = CreateSubmodelVersion(
    "core",
    std::vector{CreateDefaultSubmodel("a")},
    std::vector{CreateSubmodelFeature("a", SubmodelRequireMode::SELECTIVE)}
  );

  auto a_default = CreateSubmodelVersion(
    "a_default", std::vector{CreateDefaultSubmodel("a1")}, std::vector{CreateSubmodelFeature("a1")}
  );

  auto a1_default = CreateSubmodelVersion("a1_default");
  auto a1_feature = CreateSubmodelVersion("a1_feature");

  auto a_feature = CreateSubmodelVersion(
    "a_feature",
    std::vector{CreateDefaultSubmodel("a2", SubmodelRequireMode::IMMEDIATE)},
    std::vector{
      CreateSubmodelFeature("a2", SubmodelRequireMode::SELECTIVE, "_feature_1"),
      CreateSubmodelFeature("a2", SubmodelRequireMode::RECURSIVE, "_feature_2"),
    }
  );

  // Immediate cuts the walk at a2_default: a21_default/a211_default never reached.
  auto a2_default = CreateSubmodelVersion("a2_default", std::vector{CreateDefaultSubmodel("a21")}, {});
  auto a21_default = CreateSubmodelVersion("a21_default", std::vector{CreateDefaultSubmodel("a211")}, {});
  auto a211_default = CreateSubmodelVersion("a211_default");

  // Selective walks per-edge: Selective on a22_default continues, None on a221_default stops.
  auto a2_feature_1 = CreateSubmodelVersion(
    "a2_feature_1", std::vector{CreateDefaultSubmodel("a22", SubmodelRequireMode::SELECTIVE)}, {}
  );
  auto a22_default = CreateSubmodelVersion("a22_default", std::vector{CreateDefaultSubmodel("a221")}, {});
  auto a221_default = CreateSubmodelVersion("a221_default");

  // Recursive infects the whole subtree regardless of per-edge mode.
  auto a2_feature_2 = CreateSubmodelVersion("a2_feature_2", std::vector{CreateDefaultSubmodel("a23")}, {});
  auto a23_default = CreateSubmodelVersion("a23_default", std::vector{CreateDefaultSubmodel("a231")}, {});
  auto a231_default = CreateSubmodelVersion("a231_default");

  SECTION("Add part") {
    RuntimeManager manager(core, {a_default, a1_default});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a_feature});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a2_default", "a2_feature_1", "a2_feature_2"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a2_default, a2_feature_1, a2_feature_2});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a22_default", "a23_default"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a22_default, a23_default});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a231_default"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a231_default});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.RemoveRuntimeParts({"a_feature"});
    REQUIRE(action == UpdateAction{
      .freed_parts = {
        "a2_default",
        "a2_feature_1", "a22_default",
        "a2_feature_2", "a23_default", "a231_default",
      },
      .required_versions = {"a_feature"}, .consumed_versions = action.consumed_versions,
    });
  }

  SECTION("Pre existing parts") {
    RuntimeManager manager(
      core,
      {a_default,
       a1_default,
       a1_feature,
       a_feature,
       a2_default,
       a21_default,
       a211_default,
       a2_feature_1,
       a22_default,
       a221_default,
       a2_feature_2,
       a23_default,
       a231_default}
    );
    auto action = manager.InitialUpdateState();
    REQUIRE(action == UpdateAction{
      .freed_parts = {
        "a1_feature",
        "a21_default", "a211_default",
        "a221_default",
      },
      .required_versions = {}, .consumed_versions = action.consumed_versions,
    });
  }
}

TEST_CASE_METHOD(Fixture, "require_mode, diamond", "[runtime][submodel_manager][require_mode]") {
  // Section 1 (Imm from a_default, Rec from b_default):
  // core
  //   -> a_default (+)
  //     -> a1_default(+)
  //     -> a1_feature(-, immediate)
  //       -> a11_default(-)
  //   -> b_default(+)
  //     -> b1_default(+)
  //     -> a1_feature(-, recursive)
  //       -> a11_default(-)
  //
  // Section 2: same versions, modes swapped.
  auto a11_default = CreateSubmodelVersion("a11_default");
  auto a1_feature = CreateSubmodelVersion("a1_feature", std::vector{CreateDefaultSubmodel("a11")}, {});
  auto a1_default = CreateSubmodelVersion("a1_default");
  auto b1_default = CreateSubmodelVersion("b1_default");

  SECTION("Immediate from a, Recursive from b") {
    auto a_default = CreateSubmodelVersion(
      "a_default",
      std::vector{CreateDefaultSubmodel("a1")},
      std::vector{CreateSubmodelFeature("a1", SubmodelRequireMode::IMMEDIATE)}
    );
    auto b_default = CreateSubmodelVersion(
      "b_default",
      std::vector{CreateDefaultSubmodel("b1")},
      std::vector{CreateSubmodelFeature("a1", SubmodelRequireMode::RECURSIVE)}
    );
    auto core = CreateSubmodelVersion("core", std::vector{CreateDefaultSubmodel("a"), CreateDefaultSubmodel("b")}, {});

    RuntimeManager manager(core, {a_default, a1_default, b_default, b1_default});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a1_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a1_feature});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a11_default"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a11_default});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.RemoveRuntimeParts({"a1_feature"});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {"a11_default"},
        .required_versions = {"a1_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );
  }

  SECTION("Recursive from a, Immediate from b") {
    auto a_default = CreateSubmodelVersion(
      "a_default",
      std::vector{CreateDefaultSubmodel("a1")},
      std::vector{CreateSubmodelFeature("a1", SubmodelRequireMode::RECURSIVE)}
    );
    auto b_default = CreateSubmodelVersion(
      "b_default",
      std::vector{CreateDefaultSubmodel("b1")},
      std::vector{CreateSubmodelFeature("a1", SubmodelRequireMode::IMMEDIATE)}
    );
    auto core = CreateSubmodelVersion("core", std::vector{CreateDefaultSubmodel("a"), CreateDefaultSubmodel("b")}, {});

    RuntimeManager manager(core, {a_default, a1_default, b_default, b1_default});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a1_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a1_feature});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a11_default"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a11_default});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.RemoveRuntimeParts({"a1_feature"});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {"a11_default"},
        .required_versions = {"a1_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );
  }
}

TEST_CASE_METHOD(Fixture, "require_mode, in active version", "[runtime][submodel_manager][require_mode]") {
  // core
  //   -> a_default (+)
  //     -> a1_default(+, selective)
  //       -> a11_default(+)
  //       -> a11_feature(-, immediate)
  auto a11_default = CreateSubmodelVersion("a11_default");
  auto a11_feature = CreateSubmodelVersion("a11_feature");
  auto a1_default = CreateSubmodelVersion(
    "a1_default",
    std::vector{CreateDefaultSubmodel("a11")},
    std::vector{CreateSubmodelFeature("a11", SubmodelRequireMode::IMMEDIATE)}
  );
  auto a_default = CreateSubmodelVersion(
    "a_default", std::vector{CreateDefaultSubmodel("a1", SubmodelRequireMode::SELECTIVE)}, {}
  );
  auto core = CreateSubmodelVersion("core", std::vector{CreateDefaultSubmodel("a")}, {});

  SECTION("Add part") {
    RuntimeManager manager(core, {a_default, a1_default, a11_default});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a11_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.AddRuntimeParts({a11_feature});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );

    action = manager.RemoveRuntimeParts({"a11_feature"});
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {"a11_feature"},
        .consumed_versions = action.consumed_versions,
      }
    );
  }

  SECTION("All parts present") {
    RuntimeManager manager(core, {a_default, a1_default, a11_default, a11_feature});
    auto action = manager.InitialUpdateState();
    REQUIRE(
      action
      == UpdateAction{
        .freed_parts = {},
        .required_versions = {},
        .consumed_versions = action.consumed_versions,
      }
    );
  }
}

TEST_CASE_METHOD(
  Fixture, "available_features are collected from active graph", "[runtime][submodel_manager][available_features]"
) {
  auto core = CreateSubmodelVersion(
    "core", std::vector{CreateDefaultSubmodel("a")}, std::vector{CreateSubmodelFeature("a")}
  );
  auto a_default = CreateSubmodelVersion("a_default");
  auto a_feature = CreateSubmodelVersion(
    "a_feature", std::vector{CreateDefaultSubmodel("asr", SubmodelRequireMode::IMMEDIATE)}, {}
  );

  RuntimeManager manager(core, {a_default, a_feature});

  REQUIRE(
    manager.GetMissingSubmodelVersions(std::unordered_set<std::string>{"a_feature"})
    == std::unordered_set<std::string>{
      "asr_default",
    }
  );

  auto asr_default = CreateSubmodelVersion("asr_default");
  RuntimeManager ready_manager(core, {a_default, a_feature, asr_default});
  REQUIRE(ready_manager.GetMissingSubmodelVersions(std::unordered_set<std::string>{"a_feature"}).empty());
}

TEST_CASE_METHOD(
  Fixture,
  "missing versions for submodel version chains preserve edge require_mode",
  "[runtime][submodel_manager][require_mode]"
) {
  // spotter
  //   -> recorder (-, checked directly by chain, recursive)
  //     -> asr_default (-)
  //     -> eou_default (-)
  auto core = CreateSubmodelVersion("core");
  auto spotter = CreateSubmodelVersion(
    "spotter",
    std::unordered_map<std::string, DefaultSubmodel>{},
    std::vector{SubmodelFeature{
      .feature_ids = {"voice_record"},
      .submodel_id = "recorder",
      .submodel_version = "recorder_feature",
      .require_mode = SubmodelRequireMode::RECURSIVE,
    }}
  );
  auto recorder = CreateSubmodelVersion(
    "recorder_feature",
    std::vector{
      CreateDefaultSubmodel("asr"),
      CreateDefaultSubmodel("eou"),
    },
    {}
  );

  RuntimeManager manager(core, {spotter, recorder});

  REQUIRE(manager.GetMissingSubmodelVersions(std::unordered_set<std::string>{"recorder_feature"}).empty());
  REQUIRE(
    manager.GetMissingSubmodelVersions(std::vector{SubmodelVersionChain{.ids_chain = {
      {.id = "spotter", .version = "spotter"},
      {.id = "recorder", .version = "recorder_feature"},
    }}})
    == std::unordered_set<std::string>{"asr_default", "eou_default"}
  );
}

TEST_CASE_METHOD(
  Fixture,
  "missing versions for submodel version chains require declared parent edge",
  "[runtime][submodel_manager][require_mode]"
) {
  auto core = CreateSubmodelVersion("core");
  auto spotter = CreateSubmodelVersion(
    "spotter",
    std::unordered_map<std::string, DefaultSubmodel>{},
    std::vector{SubmodelFeature{
      .feature_ids = {"voice_record"},
      .submodel_id = "recorder",
      .submodel_version = "recorder_feature",
      .require_mode = SubmodelRequireMode::RECURSIVE,
    }}
  );
  auto recorder = CreateSubmodelVersion("recorder_feature");

  RuntimeManager manager(core, {spotter, recorder});

  REQUIRE_THROWS_WITH(
    manager.GetMissingSubmodelVersions(std::vector{SubmodelVersionChain{.ids_chain = {
      {.id = "spotter", .version = "spotter"},
      {.id = "unexpected_recorder", .version = "recorder_feature"},
    }}}),
    "Cannot find submodel edge from parent version 'spotter' to child 'unexpected_recorder#recorder_feature' "
    "for version chain 'spotter#spotter#unexpected_recorder#recorder_feature'"
  );
}

TEST_CASE_METHOD(Fixture, "missing versions for root submodel versions", "[runtime][submodel_manager][submodels]") {
  // core
  //   -> a_default (+)
  //   -> a_feature (-, checked directly)
  //     -> asr_default (-, immediate)
  auto core = CreateSubmodelVersion(
    "core", std::vector{CreateDefaultSubmodel("a")}, std::vector{CreateSubmodelFeature("a")}
  );

  auto a_default = CreateSubmodelVersion("a_default");
  a_default.permanent = true;

  auto a_feature = CreateSubmodelVersion(
    "a_feature", std::unordered_map<std::string, DefaultSubmodel>{}, std::vector{CreateSubmodelFeature("b")}
  );

  RuntimeManager manager(core, {a_default});

  auto action = manager.InitialUpdateState();
  REQUIRE(
    action
    == UpdateAction{
      .freed_parts = {},
      .required_versions = {},
      .consumed_versions = action.consumed_versions,
      .available_features = {"a_feature"},
    }
  );

  action = manager.AddFeature("a_feature");
  REQUIRE(
    action
    == UpdateAction{
      .freed_parts = {},
      .required_versions = {"a_feature"},
      .consumed_versions = action.consumed_versions,
      .available_features = {"a_feature"},
    }
  );

  action = manager.AddRuntimeParts({a_feature});
  REQUIRE(
    action
    == UpdateAction{
      .freed_parts = {},
      .required_versions = {},
      .consumed_versions = action.consumed_versions,
      .available_features = {"a_feature", "b_feature"},
    }
  );

  action = manager.RemoveFeature("a_feature");
  REQUIRE(
    action
    == UpdateAction{
      .freed_parts = {"a_feature"},
      .required_versions = {},
      .consumed_versions = action.consumed_versions,
      .available_features = {"a_feature"},
    }
  );
}

TEST_CASE_METHOD(Fixture, "UpdateAction reports consumed_versions", "[runtime][submodel_manager][require_mode]") {
  auto core = CreateSubmodelVersion(
    "core", std::vector{CreateDefaultSubmodel("a")}, std::vector{CreateSubmodelFeature("a")}
  );

  auto a_default = CreateSubmodelVersion("a_default");
  a_default.permanent = true;

  auto a_feature = CreateSubmodelVersion("a_feature");
  a_feature.permanent = true;

  RuntimeManager manager(core, {a_default, a_feature});

  auto action = manager.InitialUpdateState();
  REQUIRE(
    action
    == UpdateAction{
      .freed_parts = {},
      .required_versions = {},
      .consumed_versions = {{"a", "a_default"}},
    }
  );

  action = manager.AddFeature("a_feature");
  REQUIRE(
    action
    == UpdateAction{
      .freed_parts = {},
      .required_versions = {},
      .consumed_versions = {{"a", "a_feature"}},
    }
  );
}

TEST_CASE_METHOD(
  Fixture, "Submodel metadata contains inactive recursive versions", "[runtime][submodel_manager][metadata]"
) {
  auto core = CreateSubmodelVersion(
    "core", std::vector{CreateDefaultSubmodel("a")}, std::vector{CreateSubmodelFeature("a")}
  );
  auto a_default = CreateSubmodelVersion("a_default");
  auto a_feature = CreateSubmodelVersion(
    "a_feature", std::vector{CreateDefaultSubmodel("a1")}, std::vector{CreateSubmodelFeature("a2")}
  );
  auto a1 = CreateSubmodelVersion("a1_default");
  auto a2 = CreateSubmodelVersion("a2_feature");

  RuntimeManager manager(core, {a_default, a_feature, a1, a2}, {}, {"a_feature"});
  const auto metadata = manager.CollectSubmodelsMeta(
    {{
      .ids_chain = {{.id = "a", .version = "a_feature"}},
    }},
    SubmodelRequireMode::RECURSIVE
  );

  REQUIRE(metadata.has_value());
  std::unordered_set<std::string> version_ids;
  for (const auto& meta : *metadata) {
    version_ids.insert(meta.version_id);
    REQUIRE(meta.config_version.has_value());
  }
  REQUIRE(version_ids == std::unordered_set<std::string>{"a_feature", "a1_default", "a2_feature"});
}

TEST_CASE_METHOD(Fixture, "Submodel metadata allows config without version", "[runtime][submodel_manager][metadata]") {
  auto core = CreateSubmodelVersion("core");
  auto config = MakeVersionConfig({}, {}, true);
  config.erase(0, std::string{"version: test\n"}.size());
  WriteConfig(tmp.path / "core", config);

  RuntimeManager manager(core);
  const auto metadata = manager.CollectSubmodelsMeta({{}}, SubmodelRequireMode::RECURSIVE);
  REQUIRE(metadata.has_value());
  REQUIRE(metadata->size() == 1);
  REQUIRE(metadata->front().config_version == std::optional<std::string>{""});
}

TEST_CASE_METHOD(Fixture, "Submodel metadata marks unavailable models", "[runtime][submodel_manager][metadata]") {
  auto core = CreateSubmodelVersion("core");
  RuntimeManager manager(core);

  const auto metadata = manager.CollectSubmodelsMeta(
    {{
      .ids_chain = {{.id = "missing", .version = "missing"}},
    }},
    SubmodelRequireMode::RECURSIVE
  );
  REQUIRE(metadata.has_value());
  REQUIRE(metadata->size() == 1);
  REQUIRE(metadata->front().version_id == "missing");
  REQUIRE_FALSE(metadata->front().config_version.has_value());
}

namespace {

using Features = std::unordered_set<std::string>;

SubmodelFeature Rule(Features feature_ids, std::string slot, std::string version) {
  return {
    .feature_ids = std::move(feature_ids),
    .submodel_id = std::move(slot),
    .submodel_version = std::move(version),
  };
}

bool ErasePart(std::vector<RuntimePart>& parts, const std::string& id) {
  const auto size = parts.size();
  parts.erase(
    std::remove_if(
      parts.begin(),
      parts.end(),
      [&](const auto& part) {
        return part.id == id;
      }
    ),
    parts.end()
  );
  return parts.size() != size;
}

RuntimePart Permanent(RuntimePart part) {
  part.permanent = true;
  return part;
}

const char* const kBrokenConfig = "version: test\nnodes:\n  - type: no_such_node_type\n    name: broken\n";

} // namespace

TEST_CASE_METHOD(
  Fixture,
  "a downloaded version that does not build is not requested again, its part is kept",
  "[runtime][submodel_manager][parts]"
) {
  // The client (the model updater on the host) repeats SetState while what was freed gets deleted or what was
  // requested is found among the downloads; freeing and requesting again the part of a version that does not build
  // would never end.
  auto core = CreateSubmodelVersion("core", std::vector{CreateDefaultSubmodel("a")}, {CreateSubmodelFeature("a")});
  auto a_default = Permanent(CreateSubmodelVersion("a_default"));
  const auto part_of = [this](const std::string& part_id, const std::string& version, const std::string& config) {
    WriteConfig(tmp.path / part_id, config);
    return RuntimePart{
      .id = part_id,
      .data = ModelFileData{(tmp.path / part_id).string()},
      .versions = {{version, {.config_path = "config.yaml"}}},
    };
  };
  const auto client_update =
    [](
      RuntimeManager& manager,
      std::vector<RuntimePart>& parts,
      const std::vector<RuntimePart>& downloaded,
      const Features& features
    ) {
      UpdateAction action;
      int rounds = 0;
      for (bool changed = true; changed && rounds < 10; ++rounds) {
        action = manager.SetState(parts, features);
        changed = false;
        for (const auto& freed : action.freed_parts) {
          changed |= ErasePart(parts, freed);
        }
        for (const auto& version : action.required_versions) {
          for (const auto& part : downloaded) {
            if (part.versions.count(version) > 0) {
              parts.push_back(part);
              changed = true;
            }
          }
        }
      }
      return std::pair{action, rounds};
    };
  const auto broken = part_of("a_feature_broken", "a_feature", kBrokenConfig);

  SECTION("The client stops at once, every update gives the same action, the feature fails") {
    std::vector<RuntimePart> parts{core, a_default, broken};
    RuntimeManager manager(core, {a_default});
    for (int update = 0; update < 3; ++update) {
      INFO("update " << update);
      const auto [action, rounds] = client_update(manager, parts, {broken}, {"a_feature"});
      REQUIRE(rounds == 1);
      REQUIRE(action.required_versions.empty());
      REQUIRE(action.freed_parts.empty());
      REQUIRE(action.consumed_versions.at("a") == "a_default");
      REQUIRE(manager.HasPart("a_feature_broken"));
      REQUIRE(action.inactive_versions == Features{"a_feature"});
    }
  }

  SECTION("A part of the same version that builds replaces the broken one") {
    std::vector<RuntimePart> parts{core, a_default, broken};
    RuntimeManager manager(core, {a_default});
    REQUIRE(client_update(manager, parts, {}, {"a_feature"}).first.inactive_versions == Features{"a_feature"});

    parts = {core, a_default, part_of("a_feature_fixed", "a_feature", MakeVersionConfig())};
    const auto [action, rounds] = client_update(manager, parts, {}, {"a_feature"});
    REQUIRE(rounds == 1);
    REQUIRE(action.consumed_versions.at("a") == "a_feature");
    REQUIRE(action.inactive_versions.empty());
    REQUIRE_FALSE(manager.HasPart("a_feature_broken"));
  }

  SECTION("The disabled feature frees the part, enabling it again requests the version") {
    std::vector<RuntimePart> parts{core, a_default, broken};
    RuntimeManager manager(core, {a_default});
    client_update(manager, parts, {broken}, {"a_feature"});
    REQUIRE(manager.SetState(parts, {}).freed_parts == Features{"a_feature_broken"});
    REQUIRE_FALSE(manager.HasPart("a_feature_broken"));

    parts = {core, a_default};
    const auto [action, rounds] = client_update(manager, parts, {broken}, {"a_feature"});
    REQUIRE(rounds == 2);
    REQUIRE(action.required_versions.empty());
    REQUIRE(action.freed_parts.empty());
    REQUIRE(manager.HasPart("a_feature_broken"));
    REQUIRE(action.inactive_versions == Features{"a_feature"});
  }

  SECTION("A version whose nested part is missing requests the nested version, not itself") {
    auto a_feature = part_of(
      "a_feature_part",
      "a_feature",
      MakeVersionConfig({{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}})
    );
    std::vector<RuntimePart> parts{core, a_default, a_feature};
    RuntimeManager manager(core, {a_default});
    auto action = manager.SetState(parts, {"a_feature"});
    REQUIRE(action.consumed_versions.at("a") == "a_default");
    REQUIRE(action.required_versions == Features{"c_default"});
    REQUIRE(action.freed_parts.empty());
    REQUIRE(action.inactive_versions == Features{"a_feature", "c_default"});

    parts.push_back(CreateSubmodelVersion("c_default"));
    action = manager.SetState(parts, {"a_feature"});
    REQUIRE(action.consumed_versions.at("a") == "a_feature");
    REQUIRE(action.required_versions.empty());
    REQUIRE(action.inactive_versions.empty());
  }

  SECTION("An unreadable config of the downloaded version keeps the part and asks nothing") {
    std::vector<RuntimePart> parts{core, a_default, part_of("a_feature_garbage", "a_feature", "version: [\n")};
    RuntimeManager manager(core, {a_default});
    const auto [action, rounds] = client_update(manager, parts, {}, {"a_feature"});
    REQUIRE(rounds == 1);
    REQUIRE(action.required_versions.empty());
    REQUIRE(action.freed_parts.empty());
    REQUIRE(action.consumed_versions.at("a") == "a_default");
    REQUIRE(manager.HasPart("a_feature_garbage"));
    REQUIRE(action.inactive_versions == Features{"a_feature"});
  }

  SECTION("A nested version is requested by what the build missed, whatever its require mode") {
    for (const auto mode :
         {SubmodelRequireMode::NONE, SubmodelRequireMode::SELECTIVE, SubmodelRequireMode::RECURSIVE}) {
      INFO(EnumToString(mode));
      Fixture fresh;
      auto fresh_core = fresh.CreateSubmodelVersion(
        "core", std::vector{fresh.CreateDefaultSubmodel("a")}, {fresh.CreateSubmodelFeature("a")}
      );
      auto fresh_default = Permanent(fresh.CreateSubmodelVersion("a_default"));
      WriteConfig(
        fresh.tmp.path / "a_feature_part",
        MakeVersionConfig({{"c", {.submodel_version = "c_default", .require_mode = mode}}})
      );
      const RuntimePart a_feature{
        .id = "a_feature_part",
        .data = ModelFileData{(fresh.tmp.path / "a_feature_part").string()},
        .versions = {{"a_feature", {.config_path = "config.yaml"}}},
      };
      RuntimeManager manager(fresh_core, {fresh_default});
      const auto action = manager.SetState({fresh_core, fresh_default, a_feature}, {"a_feature"});
      REQUIRE(action.required_versions == Features{"c_default"});
      REQUIRE(action.freed_parts.empty());
      REQUIRE(action.inactive_versions.count("a_feature") == 1);
    }
  }

  SECTION("A nested version that is downloaded but does not build keeps its part too") {
    for (const auto& config : {std::string{kBrokenConfig}, std::string{"version: [\n"}}) {
      INFO(config);
      auto a_feature = part_of(
        "a_feature_part",
        "a_feature",
        MakeVersionConfig({{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}})
      );
      auto c_part = part_of("c_part", "c_default", config);
      std::vector<RuntimePart> parts{core, a_default, a_feature, c_part};
      RuntimeManager manager(core, {a_default});
      const auto [action, rounds] = client_update(manager, parts, {c_part}, {"a_feature"});
      REQUIRE(rounds == 1);
      REQUIRE(action.required_versions.empty());
      REQUIRE(action.freed_parts.empty());
      REQUIRE(manager.HasPart("c_part"));
      REQUIRE(action.inactive_versions.count("c_default") == 1);
    }
  }

  SECTION("An unreadable config of one version does not hide what another one misses") {
    auto two_core = CreateSubmodelVersion(
      "core",
      std::vector{CreateDefaultSubmodel("a"), CreateDefaultSubmodel("b")},
      {CreateSubmodelFeature("a"), CreateSubmodelFeature("b")}
    );
    auto b_default = Permanent(CreateSubmodelVersion("b_default"));
    auto b_feature = part_of(
      "b_feature_part",
      "b_feature",
      MakeVersionConfig({{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}})
    );
    RuntimeManager manager(two_core, {a_default, b_default});
    const auto action = manager.SetState(
      {two_core, a_default, b_default, part_of("a_feature_garbage", "a_feature", "version: [\n"), b_feature},
      {"a_feature", "b_feature"}
    );
    REQUIRE(action.required_versions == Features{"c_default"});
    REQUIRE(action.freed_parts.empty());
    REQUIRE(action.inactive_versions.count("a_feature") == 1);
    REQUIRE(action.inactive_versions.count("b_feature") == 1);
  }

  SECTION("A built slot asked for a broken version stays on the built one, going back frees the broken part") {
    auto f_core = CreateSubmodelVersion(
      "core",
      std::vector{CreateDefaultSubmodel("a")},
      {CreateSubmodelFeature("a", SubmodelRequireMode::NONE, "_f1"),
       CreateSubmodelFeature("a", SubmodelRequireMode::NONE, "_f2")}
    );
    auto f1 = CreateSubmodelVersion("a_f1");
    auto f2 = part_of("a_f2_part", "a_f2", kBrokenConfig);
    std::vector<RuntimePart> parts{f_core, a_default, f1, f2};
    RuntimeManager manager(f_core, {a_default});
    auto [action, rounds] = client_update(manager, parts, {f1, f2}, {"a_f1"});
    REQUIRE(action.consumed_versions.at("a") == "a_f1");
    REQUIRE_FALSE(manager.HasPart("a_f2_part"));

    parts.push_back(f2);
    std::tie(action, rounds) = client_update(manager, parts, {f1, f2}, {"a_f2"});
    REQUIRE(rounds == 1);
    REQUIRE(action.consumed_versions.at("a") == "a_f1");
    REQUIRE(action.required_versions.empty());
    REQUIRE(action.freed_parts.empty());
    REQUIRE(action.inactive_versions == Features{"a_f2"});

    std::tie(action, rounds) = client_update(manager, parts, {f1, f2}, {"a_f1"});
    REQUIRE(action.consumed_versions.at("a") == "a_f1");
    REQUIRE_FALSE(manager.HasPart("a_f2_part"));

    std::tie(action, rounds) = client_update(manager, parts, {f1, f2}, {});
    REQUIRE(action.consumed_versions.at("a") == "a_default");
    REQUIRE_FALSE(manager.HasPart("a_f1"));
  }

  SECTION("The broken and the fixed part of one version come together: the fixed one builds, the broken one goes") {
    std::vector<RuntimePart> parts{
      core, a_default, broken, part_of("a_feature_fixed", "a_feature", MakeVersionConfig())
    };
    RuntimeManager manager(core, {a_default});
    for (int update = 0; update < 2; ++update) {
      INFO("update " << update);
      const auto [action, rounds] = client_update(manager, parts, {}, {"a_feature"});
      REQUIRE(action.consumed_versions.at("a") == "a_feature");
      REQUIRE(action.inactive_versions.empty());
      REQUIRE(manager.HasPart("a_feature_fixed"));
      REQUIRE_FALSE(manager.HasPart("a_feature_broken"));
    }
  }

  SECTION("A nested required version without a part is requested instead of the fallback that failed") {
    const auto feature_config = MakeVersionConfig(
      {{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}},
      {Rule({"a_feature"}, "c", "c_x")}
    );
    SECTION("The fallback is broken") {
      auto c_default = part_of("c_part", "c_default", kBrokenConfig);
      std::vector<RuntimePart> parts{
        core, a_default, part_of("a_feature_part", "a_feature", feature_config), c_default
      };
      RuntimeManager manager(core, {a_default});
      const auto [action, rounds] = client_update(manager, parts, {c_default}, {"a_feature"});
      REQUIRE(rounds == 1);
      REQUIRE(action.required_versions == Features{"c_x"});
      REQUIRE(action.freed_parts.empty());
      REQUIRE(manager.HasPart("c_part"));
    }
    SECTION("The fallback is not downloaded either") {
      std::vector<RuntimePart> parts{core, a_default, part_of("a_feature_part", "a_feature", feature_config)};
      RuntimeManager manager(core, {a_default});
      const auto action = manager.SetState(parts, {"a_feature"});
      REQUIRE(action.required_versions == Features{"c_x"});

      parts.push_back(CreateSubmodelVersion("c_x"));
      // A built version also requests the default variant of the slot (a selective edge), as any active one does.
      const auto ready = manager.SetState(parts, {"a_feature"});
      REQUIRE(ready.consumed_versions.at("a") == "a_feature");
      REQUIRE(ready.required_versions == Features{"c_default"});
      REQUIRE(ready.inactive_versions.empty());
    }
  }

  SECTION("Nested siblings: the one built before the other failed keeps its part") {
    for (const auto mode : {SubmodelRequireMode::NONE, SubmodelRequireMode::SELECTIVE}) {
      INFO(EnumToString(mode));
      const auto two_children = MakeVersionConfig(
        {{"c", {.submodel_version = "c_default", .require_mode = mode}},
         {"e", {.submodel_version = "e_default", .require_mode = mode}}}
      );
      // Both are missing: each arrives once and the version is ready.
      const auto c_default = part_of("c_part", "c_default", MakeVersionConfig());
      const auto e_default = part_of("e_part", "e_default", MakeVersionConfig());
      std::vector<RuntimePart> parts{core, a_default, part_of("a_feature_part", "a_feature", two_children)};
      RuntimeManager manager(core, {a_default});
      auto [action, rounds] = client_update(manager, parts, {c_default, e_default}, {"a_feature"});
      REQUIRE(rounds <= 3);
      REQUIRE(action.consumed_versions.at("a") == "a_feature");
      REQUIRE(action.inactive_versions.empty());

      // One is broken: the client stops at once, the other one stays.
      const auto c_broken = part_of("c_broken", "c_default", kBrokenConfig);
      parts = {core, a_default, part_of("a_feature_part", "a_feature", two_children), c_broken, e_default};
      RuntimeManager broken_manager(core, {a_default});
      std::tie(action, rounds) = client_update(broken_manager, parts, {c_broken, e_default}, {"a_feature"});
      REQUIRE(rounds == 1);
      REQUIRE(action.required_versions.empty());
      REQUIRE(action.freed_parts.empty());
      REQUIRE(broken_manager.HasPart("e_part"));
      REQUIRE(action.inactive_versions.count("c_default") == 1);
    }
  }

  SECTION(
    "A nested sibling keeps its own nested parts while the other sibling is broken; repair and disable free right"
  ) {
    const auto sel = SubmodelRequireMode::SELECTIVE;
    const auto a_feature = part_of(
      "a_feature_part",
      "a_feature",
      MakeVersionConfig(
        {{"c", {.submodel_version = "c_default", .require_mode = sel}},
         {"e", {.submodel_version = "e_default", .require_mode = sel}}}
      )
    );
    const auto c_default = part_of(
      "c_part", "c_default", MakeVersionConfig({{"g", {.submodel_version = "g_default", .require_mode = sel}}})
    );
    const auto g_default = part_of("g_part", "g_default", MakeVersionConfig());
    const auto e_broken = part_of("e_part", "e_default", kBrokenConfig);
    std::vector<RuntimePart> parts{core, a_default, a_feature, c_default, g_default, e_broken};
    RuntimeManager manager(core, {a_default});
    auto [action, rounds] = client_update(manager, parts, {c_default, g_default, e_broken}, {"a_feature"});
    REQUIRE(rounds == 1);
    REQUIRE(action.freed_parts.empty());
    REQUIRE(action.required_versions.empty());
    for (const auto* id : {"a_feature_part", "c_part", "g_part", "e_part"}) {
      INFO(id);
      REQUIRE(manager.HasPart(id));
    }
    REQUIRE(action.inactive_versions.count("e_default") == 1);

    SECTION("The fixed sibling makes the version ready, the broken part goes") {
      ErasePart(parts, "e_part");
      parts.push_back(part_of("e_fixed", "e_default", MakeVersionConfig()));
      std::tie(action, rounds) = client_update(manager, parts, {}, {"a_feature"});
      REQUIRE(rounds == 1);
      REQUIRE(action.consumed_versions.at("a") == "a_feature");
      REQUIRE(action.inactive_versions.empty());
    }
    SECTION("The disabled feature frees the whole subtree") {
      std::tie(action, rounds) = client_update(manager, parts, {}, {});
      REQUIRE(action.consumed_versions.at("a") == "a_default");
      for (const auto* id : {"a_feature_part", "c_part", "g_part", "e_part"}) {
        INFO(id);
        REQUIRE_FALSE(manager.HasPart(id));
      }
    }
  }

  SECTION("The rule version and the fallback both broken: both are inactive, nothing is requested") {
    const auto feature_config = MakeVersionConfig(
      {{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}},
      {Rule({"a_feature"}, "c", "c_x")}
    );
    std::vector<RuntimePart> parts{
      core,
      a_default,
      part_of("a_feature_part", "a_feature", feature_config),
      part_of("c_x_part", "c_x", "version: test\nnodes:\n  - type: no_such_node_type\n    name: rule_broken\n"),
      part_of("c_default_part", "c_default", kBrokenConfig),
    };
    RuntimeManager manager(core, {a_default});
    const auto [action, rounds] = client_update(manager, parts, {}, {"a_feature"});
    REQUIRE(rounds == 1);
    REQUIRE(action.required_versions.empty());
    REQUIRE(action.inactive_versions.count("c_x") == 1);
    REQUIRE(action.inactive_versions.count("c_default") == 1);
  }

  SECTION("Three levels: the parent of the missing version keeps its part") {
    std::vector<RuntimePart> parts{
      core,
      a_default,
      part_of(
        "a_feature_part",
        "a_feature",
        MakeVersionConfig({{"b", {.submodel_version = "b_default", .require_mode = SubmodelRequireMode::SELECTIVE}}})
      ),
      part_of(
        "b_part",
        "b_default",
        MakeVersionConfig({{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}})
      ),
    };
    RuntimeManager manager(core, {a_default});
    const auto [action, rounds] = client_update(manager, parts, {}, {"a_feature"});
    REQUIRE(rounds == 1);
    REQUIRE(action.required_versions == Features{"c_default"});
    REQUIRE(action.freed_parts.empty());
    REQUIRE(manager.HasPart("b_part"));
    REQUIRE(action.inactive_versions.count("c_default") == 1);
  }

  SECTION("A built slot that cannot fall back still asks for the version it requires") {
    // The slot is on a_x; feature y needs a_y, which is missing, and the fallback a_default cannot be built without
    // c_default: the slot stays on a_x.
    auto xy_core = CreateSubmodelVersion(
      "core",
      std::vector{CreateDefaultSubmodel("a")},
      {CreateSubmodelFeature("a", SubmodelRequireMode::NONE, "_x"),
       CreateSubmodelFeature("a", SubmodelRequireMode::NONE, "_y")}
    );
    auto fallback = part_of(
      "a_default_part",
      "a_default",
      MakeVersionConfig({{"c", {.submodel_version = "c_default", .require_mode = SubmodelRequireMode::SELECTIVE}}})
    );
    const auto c_default = CreateSubmodelVersion("c_default");
    std::vector<RuntimePart> parts{xy_core, fallback, c_default, CreateSubmodelVersion("a_x")};
    RuntimeManager manager(xy_core, {fallback, c_default});
    REQUIRE(manager.SetState(parts, {"a_x"}).consumed_versions.at("a") == "a_x");
    ErasePart(parts, "c_default");
    const auto [action, rounds] = client_update(manager, parts, {}, {"a_y"});
    REQUIRE(action.consumed_versions.at("a") == "a_x");
    REQUIRE(action.required_versions.count("a_y") == 1);
    REQUIRE(action.inactive_versions.count("a_y") == 1);
  }

  SECTION("A built slot keeps the part of the broken fallback while the required version is missing") {
    // The slot is on a_x; a_y is not downloaded, so the fallback was being built and it is broken: keep its part and
    // request a_y.
    auto xy_core = CreateSubmodelVersion(
      "core",
      std::vector{CreateDefaultSubmodel("a")},
      {CreateSubmodelFeature("a", SubmodelRequireMode::NONE, "_x"),
       CreateSubmodelFeature("a", SubmodelRequireMode::NONE, "_y")}
    );
    const auto a_x = CreateSubmodelVersion("a_x");
    std::vector<RuntimePart> parts{xy_core, a_default, a_x};
    RuntimeManager manager(xy_core, {a_default});
    REQUIRE(manager.SetState(parts, {"a_x"}).consumed_versions.at("a") == "a_x");
    parts = {xy_core, a_x, part_of("a_default_broken", "a_default", kBrokenConfig)};
    const auto [action, rounds] = client_update(manager, parts, {}, {"a_y"});
    REQUIRE(rounds == 1);
    REQUIRE(action.consumed_versions.at("a") == "a_x");
    REQUIRE(action.required_versions == Features{"a_y"});
    REQUIRE(action.freed_parts.empty());
    REQUIRE(manager.HasPart("a_default_broken"));
    REQUIRE(action.inactive_versions == Features{"a_y", "a_default"});
  }

  SECTION("A version that is not downloaded is still requested") {
    std::vector<RuntimePart> parts{core, a_default};
    RuntimeManager manager(core, {a_default});
    const auto action = manager.SetState(parts, {"a_feature"});
    REQUIRE(action.required_versions == Features{"a_feature"});
    REQUIRE(action.inactive_versions == Features{"a_feature"});
  }
}
