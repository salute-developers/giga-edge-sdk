#include "giga_edge/lib/node/tests/node_test_utils/node_test_utils.h"
#include "giga_edge/modules/llm/nodes.h"

#include <catch.hpp>

using namespace giga_edge;

TEST_CASE("llm_generate reports a disabled backend at graph creation", "[llm][nodes]") {
  REQUIRE_THROWS_WITH(
    CreateNode("llm_generate", "llm_interpreter_params: {model_path: unused.gguf}"),
    "Llama.cpp backend is not supported (LLAMA_CPP_ENABLED=OFF)"
  );
}
