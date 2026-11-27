cmake_minimum_required(VERSION 3.13)

set(LLM_BACKEND_TESTS src/backend_unavailable.cpp)
if(LLAMA_CPP_ENABLED)
  set(LLM_BACKEND_TESTS
    src/interpreter/llm/llm_interface.cpp
    src/nodes/llm_generate_registry.cpp
  )
endif()

add_giga_edge_test(
  llm_tests
  SRCS
    src/llama_log.cpp
    src/kv_transcript.cpp
    src/prompt_planner.cpp
    src/special_text_escape.cpp
    src/parsers/xml_tool_call_parser.cpp
    src/nodes/llm_passthrough.cpp
    src/detail/llm_local_debug.cpp
    ${LLM_BACKEND_TESTS}
  LIBS
    llm_module
    runtime_lib
    node_test_utils
    test_utils_lib
    $<$<NOT:$<PLATFORM_ID:Windows>>:dl>
)
