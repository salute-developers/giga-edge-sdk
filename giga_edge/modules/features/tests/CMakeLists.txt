cmake_minimum_required(VERSION 3.13)

set(FEATURES_TEST_SRCS
  src/nodes/features_config.cpp
  src/nodes/feature_iterator_with_reset.cpp
)

add_giga_edge_test(
  features_tests
  SRCS
    ${FEATURES_TEST_SRCS}
  LIBS
    audio_lib
    features_module
    node_test_utils
    test_utils_lib
)