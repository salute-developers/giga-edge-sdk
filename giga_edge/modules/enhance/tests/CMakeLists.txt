cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  enhance_module_tests
  SRCS
    src/nodes/audio_processing.cpp
    src/nodes/aec.cpp
  LIBS
    enhance_module
    node_test_utils
)