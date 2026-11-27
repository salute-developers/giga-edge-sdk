cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  synthesis_text_tests
  SRCS
    src/nodes/synthesis_text_buffer.cpp
    src/nodes/synthesis_text.cpp
  LIBS
    synthesis_text_module
    node_test_utils
)