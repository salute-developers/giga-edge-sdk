# Writing a node

A node is a C++ class derived from `NodeBase` (`giga_edge/lib/node`). It declares typed inputs and outputs with
macros, parses its params from the YAML config and registers itself under a type name that configs refer to. This page
walks through a real node of the `assistant_core` module, `user_query_from_text` (code abridged): it wraps the text of
the tick into a `UserQuery` and numbers the requests.

## Files

A node lives in a module (see [writing-a-module.md](writing-a-module.md)):

```
giga_edge/modules/assistant_core/
├── internal/giga_edge/modules/assistant_core/
│   ├── nodes.h                              # includes the header of every node of the module
│   └── nodes/user_query_from_text/node.h    # params, class, registration
├── src/nodes/user_query_from_text.cpp       # params parsing and logic
└── tests/src/input_nodes.cpp                # unit tests
```

Node headers are internal: they are not installed with the SDK. The host never creates nodes directly; it loads a
config, and the runtime creates the nodes by their registered type names.

## Header: params, ports, registration

```cpp
#pragma once

#include "giga_edge/lib/config_parser/config_parser_fwd.h"
#include "giga_edge/lib/node/node.h"
#include "giga_edge/lib/node/node_registry.h"
#include "giga_edge/modules/assistant_core/contracts/types.h"

#include <cstdint>
#include <string>

namespace giga_edge {

struct UserQueryFromTextParams {
  std::string request_id_prefix = "request-";
};

template<>
UserQueryFromTextParams ParseConfig(const YAML::Node& config);

class UserQueryFromText : public NodeBase {
public:
  UserQueryFromText(const NodeBaseConfig& base_config, const UserQueryFromTextParams& params);

  void ProcessOnce() override;
  void Reset() override;

private:
  REGISTER_INPUT(user_query_from_text, text, Text, std::string, "Recognized or typed user text; empty on idle ticks.");
  REGISTER_OPTIONAL_INPUT(
    user_query_from_text,
    request_id,
    RequestId,
    std::string,
    "Caller-provided request id; when absent or empty the node numbers requests with request_id_prefix."
  );
  REGISTER_OUTPUT(
    user_query_from_text, user_query, UserQuery, UserQuery, "UserQuery with a fresh request_id per non-empty text."
  );

  UserQueryFromTextParams params_;
  uint64_t next_request_id_ = 0;
};

REGISTER_NODE(user_query_from_text, UserQueryFromText, UserQueryFromTextParams, "Wraps plain text into a UserQuery.");

} // namespace giga_edge
```

A port macro takes five arguments: the node type, the port name used in configs, the name for the generated
accessor, the value type and a description:

| Macro | Generates | Meaning |
|---|---|---|
| `REGISTER_INPUT` | `const T& GetX()` | required input |
| `REGISTER_OPTIONAL_INPUT` | `std::optional<std::reference_wrapper<const T>> GetX()` | input the config may leave out |
| `REGISTER_BATCH_INPUT` | `std::vector<const T*> GetXBatch()` | the whole batch of an upstream batch output |
| `REGISTER_OUTPUT` | `PushX(T)` | one value per call of `ProcessOnce` |
| `REGISTER_BATCH_OUTPUT` | `PushX(T)` | one or more values per tick, from `ProcessOnce` and the loop iterations |
| `REGISTER_RECURSIVE_OUTPUT` | `PushX(T)` | value for the next tick; the node overrides `DoInitRecursiveOutputs()` to push the first one |
| `REGISTER_OPTIONAL_OUTPUT`, `REGISTER_OPTIONAL_BATCH_OUTPUT` | `PushX(T)`, `HasXOutput()` | output the config may leave out; a push to it is dropped |

Ports that the config defines, like the variables of the `logical_expression` node, are declared with
`REGISTER_DYNAMIC_INPUTS(type)` or `REGISTER_DYNAMIC_OUTPUTS(type)`. The node then implements `GetDynamicInputs()` or
`GetDynamicOutputs()`, which return the port names, and reads the values by name: see
`giga_edge/modules/common/internal/giga_edge/modules/common/nodes/logical_expression/node.h` and its source.

`REGISTER_NODE` comes in three forms; the constructor must match:

| Registration | Constructor |
|---|---|
| `REGISTER_NODE(type, Class, "description")` | `Class(const NodeBaseConfig&)` |
| `REGISTER_NODE(type, Class, Params, "description")` | `Class(const NodeBaseConfig&, const Params&)` |
| `REGISTER_NODE(type, Class, Params, true, "description")` | `Class(const NodeBaseConfig&, const IDataReaderPtr&, const Params&)` |

The data reader of the last form reads files of the model part the config came from, such as weights; see
`llm_generate` for a node that loads its model this way.

The descriptions end up in the node reference ([configuration.md](configuration.md#node-reference)), so write them
for the person who wires the graph.

## Source: params and logic

```cpp
#include "giga_edge/lib/config_parser/config_parser.h"
#include "giga_edge/modules/assistant_core/nodes/user_query_from_text/node.h"
#include "giga_edge/modules/assistant_core/text.h"

namespace giga_edge {

template<>
UserQueryFromTextParams ParseConfig(const YAML::Node& config) {
  UserQueryFromTextParams result;
  result.request_id_prefix = config["request_id_prefix"].as<std::string>(result.request_id_prefix);
  return result;
}

UserQueryFromText::UserQueryFromText(const NodeBaseConfig& base_config, const UserQueryFromTextParams& params)
  : NodeBase(base_config)
  , params_(params) {}

void UserQueryFromText::Reset() {
  next_request_id_ = 0;
}

void UserQueryFromText::ProcessOnce() {
  UserQuery query;
  query.text = Trim(GetText());
  if (query.text.empty()) {
    query.status = AssistantStatus::SKIPPED;
    PushUserQuery(std::move(query));
    return;
  }
  const auto request_id = GetRequestId();
  query.request_id = request_id.has_value() && !request_id.value().get().empty()
    ? request_id.value().get()
    : params_.request_id_prefix + std::to_string(next_request_id_++);
  PushUserQuery(std::move(query));
}

} // namespace giga_edge
```

The contract of `ProcessOnce`:

- it runs once per tick, when every connected input has a value;
- it pushes exactly one value into every non-batch output and at least one into every batch output. This holds on an
  idle tick too (here an empty text still produces a `SKIPPED` query), because the nodes downstream need their tick.
  Otherwise the runtime reports that the output is not closed;
- it runs on a thread of the graph pool. A node instance is never run concurrently with itself, so its own members
  need no locking; anything shared with other threads does.

`Reset()` brings the node back to its initial state; the runtime calls it when the host resets the graph.

Long work that can be split into steps, such as LLM generation, should not block the pool. For that `ProcessOnce`
calls `StartLoop()`; the runtime then calls `ProcessLoopIterationImpl()` until the node calls `FinishLoop()`, and new
input interrupts the loop through `ProcessLoopInterruptImpl()`. Graph-wide events arrive in `ProcessEventImpl()`. For
more, with pictures, see the [node README](../giga_edge/lib/node/README.md).

## Registration

Add the node header to the module's `nodes.h`:

```cpp
#include "giga_edge/modules/assistant_core/nodes/user_query_from_text/node.h"
```

The build collects `nodes.h` of every module, so the node is available to configs whenever its module is built.
Sources under `src/` are picked up by the module's CMake automatically, unless the module lists its `SRCS`.

## Test

Unit tests create the node alone, push values into its inputs and read its outputs with `node_test_utils`
(`giga_edge/lib/node/tests`):

```cpp
#include "giga_edge/lib/node/system_events.h"
#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/assistant_core/nodes.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("user_query_from_text numbers requests and skips empty ticks", "[assistant][node][input]") {
  auto ctx = CreateNode("user_query_from_text", "request_id_prefix: q-");  // type, params YAML
  NodePush<std::string>(ctx, "text", "");
  REQUIRE(NodeGet<UserQuery>(ctx, "user_query").status == AssistantStatus::SKIPPED);

  NodePush<std::string>(ctx, "text", "hi");
  REQUIRE(NodeGet<UserQuery>(ctx, "user_query").request_id == "q-0");

  NodePushEvent(ctx, Event{.event = ResetEvent{}});
  NodePush<std::string>(ctx, "text", "after reset");
  REQUIRE(NodeGet<UserQuery>(ctx, "user_query").request_id == "q-0");
}
```

Optional inputs are connected only when listed: `CreateNode("user_query_from_text", "", {}, {"request_id"})`. Batch
outputs are read with `NodeGetBatch<T>`.

The test file is listed in the module's `tests/CMakeLists.txt`:

```cmake
add_giga_edge_test(
  assistant_core_module_tests
  SRCS
    src/input_nodes.cpp
  LIBS
    assistant_core_module
    node_test_utils
)
```

To test several nodes together, build a whole graph from a config in the test. `Runtime` accepts a data reader, and
`CreateCachingDataReader(nullptr, {{"config.yaml", span}})` serves a config from memory: see
`giga_edge/modules/llm/tests/src/nodes/llm_passthrough.cpp`.

Build and run:

```bash
./tools/scripts/build_giga_edge.sh --target-os macos_arm64 --module assistant_core --build-tests
./build_macos_arm64/giga_edge/giga_edge/modules/assistant_core/tests/assistant_core_module_tests "[input]"
```

Every `TEST_CASE` is also registered in CTest under its name:
`ctest --test-dir build_macos_arm64/giga_edge -R user_query_from_text`.
