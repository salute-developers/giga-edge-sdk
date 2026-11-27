cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  function_calling_module_tests
  SRCS src/compact_tool_call_parser.cpp src/compact_tool_result.cpp
  LIBS function_calling_module node_test_utils
)
