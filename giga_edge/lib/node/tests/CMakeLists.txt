cmake_minimum_required(VERSION 3.13)

add_library(
  node_test_utils
  STATIC
  lib/node_test_utils/src/node_test_utils.cpp
)

target_include_directories(node_test_utils PUBLIC lib/node_test_utils/include)
target_link_libraries(node_test_utils PUBLIC node_lib sequence_id_generator_lib)

add_giga_edge_test(
  node_tests
  SRCS
    src/loop_scheduler_test.cpp
    src/loop_replay_node_test.cpp
    src/loop_reset_test.cpp
    src/node_output.cpp
    src/optional_output_test.cpp
  LIBS
    node_lib
    node_test_utils
    sequence_id_generator_lib
)
