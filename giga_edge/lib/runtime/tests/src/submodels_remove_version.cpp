#include "giga_edge/lib/data_reader/helpers.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/lib/runtime/runtime.h"
#include "giga_edge/nodes/all.h"

#include <catch.hpp>

#include <string_view>
#include <unordered_map>
#include <vector>

using namespace giga_edge;

namespace {

IDataSpanPtr MakeDataSpan(std::string_view text) {
  return Create<IDataSpanPtr>(std::vector<char>(text.begin(), text.end()));
}

} // namespace

TEST_CASE("Submodels, remove version whose node consumes the submodel output", "[runtime][submodels]") {
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

  constexpr std::string_view full_config = R"(
nodes:
  - type: logical_op
    name: producer
    inputs:
      first_input: signal
    outputs:
      output: out
    params:
      logical_op_type: not

  - type: logical_op
    name: internal_consumer
    inputs:
      first_input: out
    outputs:
      output: internal_unused
    params:
      logical_op_type: not

submodels:
  - id: inner
    inputs:
      trigger: out
    outputs:
      inner_out: inner_unused
)";

  constexpr std::string_view inner_config = R"(
nodes:
  - type: logical_op
    name: inner_consumer
    inputs:
      first_input: trigger
    outputs:
      output: inner_out
    params:
      logical_op_type: not
)";

  constexpr std::string_view stub_config = R"(
nodes:
  - type: logical_op
    name: stub_producer
    inputs:
      first_input: signal
    outputs:
      output: out
    params:
      logical_op_type: not
)";

  Runtime model(CreateAnyPathSameSpanDataReader(MakeDataSpan(core_config)));

  const SubmodelVersionChain full_chain{.ids_chain = {{.id = "sub", .version = "full"}}};
  const SubmodelVersionChain inner_chain{
    .ids_chain = {{.id = "sub", .version = "full"}, {.id = "inner", .version = "v"}}
  };
  const SubmodelVersionChain stub_chain{.ids_chain = {{.id = "sub", .version = "stub"}}};

  model.AddSubmodelVersion(full_chain, CreateAnyPathSameSpanDataReader(MakeDataSpan(full_config)));
  model.AddSubmodelVersion(inner_chain, CreateAnyPathSameSpanDataReader(MakeDataSpan(inner_config)));
  model.SetActiveSubmodelVersion(full_chain);
  model.SetActiveSubmodelVersion(inner_chain);
  model.Push<bool>({}, "input", true, {});

  model.AddSubmodelVersion(stub_chain, CreateAnyPathSameSpanDataReader(MakeDataSpan(stub_config)));
  model.SetActiveSubmodelVersion(stub_chain);
  model.RemoveSubmodelVersion(full_chain);

  REQUIRE_NOTHROW(model.Push<bool>({}, "input", true, {}));
}
