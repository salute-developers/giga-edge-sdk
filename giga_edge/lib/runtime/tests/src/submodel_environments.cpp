// An environment declared in the config of a submodel version is visible to the nodes of that version and of every
// submodel version nested in it; a nested version replaces it for its own subtree by declaring the same name.

#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/runtime.h"

#include <catch.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace giga_edge;

namespace {

// Version chain of the node -> the probe_env it was given.
std::map<std::string, const Environment*> g_seen;

// Cannot be created without the probe_env environment; remembers which one it got.
class EnvironmentProbeNode : public NodeBase {
public:
  explicit EnvironmentProbeNode(const NodeBaseConfig& config)
    : NodeBase(config) {
    const auto environment = config.environments->find("probe_env");
    if (environment == config.environments->end()) {
      throw std::runtime_error("no probe_env in " + config.submodel_version_chain.AsString());
    }
    g_seen[config.submodel_version_chain.AsString()] = environment->second.get();
  }

  void Reset() override {}

  void ProcessOnce() override {
    PushOut(GetIn());
  }

  REGISTER_INPUT(environment_probe_node, in, In, bool, "")
  REGISTER_OUTPUT(environment_probe_node, out, Out, bool, "")
};

REGISTER_NODE(environment_probe_node, EnvironmentProbeNode, "");

constexpr std::string_view kCoreConfig = R"(
num_threads: 0
version: test
lib_version: 1.18

submodels:
  - id: outer
    inputs:
      in: input
    outputs:
      out: out
  - id: other
    inputs:
      in: input
    outputs:
      out: other_out
)";

constexpr std::string_view kEnvironment = R"(
model_environments:
  probe_env:
    type: empty
)";

// A probe node and a nested submodel after it.
constexpr std::string_view kProbeWithSubmodel = R"(
nodes:
  - type: environment_probe_node
    name: probe
    inputs:
      in: in
    outputs:
      out: mid

submodels:
  - id: nested
    inputs:
      in: mid
    outputs:
      out: out
)";

constexpr std::string_view kProbe = R"(
nodes:
  - type: environment_probe_node
    name: probe
    inputs:
      in: in
    outputs:
      out: out
)";

IDataReaderPtr MakeReader(const std::string& text) {
  return CreateAnyPathSameSpanDataReader(Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end())));
}

SubmodelVersionChain Chain(std::vector<SubmodelVersionId> ids) {
  return {.ids_chain = std::move(ids)};
}

} // namespace

TEST_CASE("Environments of a submodel version reach the versions nested in it", "[runtime][submodels]") {
  g_seen.clear();
  Runtime model(MakeReader(std::string(kCoreConfig)));

  const auto with_environment = std::string(kEnvironment) + std::string(kProbeWithSubmodel);
  const auto without_environment = std::string(kProbeWithSubmodel);

  // outer declares the environment; its child and its grandchild do not.
  model.AddSubmodelVersion(Chain({{"outer", "v"}}), MakeReader(with_environment));
  REQUIRE_NOTHROW(
    model.AddSubmodelVersion(Chain({{"outer", "v"}, {"nested", "plain"}}), MakeReader(without_environment))
  );
  REQUIRE_NOTHROW(model.AddSubmodelVersion(
    Chain({{"outer", "v"}, {"nested", "plain"}, {"nested", "leaf"}}), MakeReader(std::string(kProbe))
  ));
  // One environment for the whole subtree.
  REQUIRE(g_seen.at("outer#v#nested#plain") == g_seen.at("outer#v"));
  REQUIRE(g_seen.at("outer#v#nested#plain#nested#leaf") == g_seen.at("outer#v"));

  // A child that declares the same name replaces it for itself and for its own children.
  model.AddSubmodelVersion(Chain({{"outer", "v"}, {"nested", "own"}}), MakeReader(with_environment));
  model.AddSubmodelVersion(
    Chain({{"outer", "v"}, {"nested", "own"}, {"nested", "leaf"}}), MakeReader(std::string(kProbe))
  );
  REQUIRE(g_seen.at("outer#v#nested#own") != g_seen.at("outer#v"));
  REQUIRE(g_seen.at("outer#v#nested#own#nested#leaf") == g_seen.at("outer#v#nested#own"));

  // Outside the subtree the environment does not exist.
  REQUIRE_THROWS(model.AddSubmodelVersion(Chain({{"other", "v"}}), MakeReader(std::string(kProbe))));
}
