#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace giga_edge;

TEST_CASE("Runtime, duplicate output name is rejected", "[runtime][config]") {
  constexpr std::string_view config = R"(
num_threads: 0
version: test
lib_version: 1.18

nodes:
  - type: logical_op
    name: first
    inputs:
      first_input: input
    outputs:
      output: out
    params:
      logical_op_type: not

  - type: logical_op
    name: second
    inputs:
      first_input: input
    outputs:
      output: out
    params:
      logical_op_type: not
)";

  const auto span = Create<IDataSpanPtr>(std::vector<char>(config.begin(), config.end()));
  REQUIRE_THROWS_WITH(
    Runtime(CreateAnyPathSameSpanDataReader(span)), Catch::Matchers::Contains("Multiple outputs with id out")
  );
}

TEST_CASE("Runtime, lib_version of a submodel version is checked when the config states it", "[runtime][config]") {
  constexpr std::string_view core_config = R"(
num_threads: 0
version: test
lib_version: 1.18

submodels:
  - id: sub
    inputs:
      signal: input
    outputs:
      out: out
)";
  constexpr std::string_view version_nodes = R"(
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
  const auto reader = [&version_nodes](const std::string& head) {
    const auto text = head + std::string(version_nodes);
    return CreateAnyPathSameSpanDataReader(Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end())));
  };
  const auto chain = [](const std::string& version) {
    return SubmodelVersionChain{.ids_chain = {{.id = "sub", .version = version}}};
  };

  const auto span = Create<IDataSpanPtr>(std::vector<char>(core_config.begin(), core_config.end()));
  Runtime model(CreateAnyPathSameSpanDataReader(span));

  // The same rule as for the top-level config: the major is that of the SDK, the minor is not above the SDK's.
  REQUIRE_NOTHROW(model.AddSubmodelVersion(chain("older_minor"), reader("lib_version: 1.0\n")));
  REQUIRE_THROWS_WITH(
    model.AddSubmodelVersion(chain("newer_minor"), reader("lib_version: 1.999\n")),
    Catch::Matchers::Contains("Giga Edge SDK runtime version unsupported")
  );
  REQUIRE_THROWS_WITH(
    model.AddSubmodelVersion(chain("other_major"), reader("lib_version: 2.0\n")),
    Catch::Matchers::Contains("Giga Edge SDK runtime version unsupported")
  );
  REQUIRE_FALSE(model.HasOutput(chain("newer_minor"), "out"));
  // Unlike the top-level config, a version may leave the field out.
  REQUIRE_NOTHROW(model.AddSubmodelVersion(chain("not_stated"), reader("")));
}
