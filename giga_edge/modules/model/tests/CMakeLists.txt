cmake_minimum_required(VERSION 3.13)

set(MODEL_TEST_SRCS
  src/5bit_tensor.cpp
  src/config_parser/interpreter.cpp
  src/model_detail.cpp
  src/nodes/model_config.cpp
  src/nodes/inference_strategies.cpp
  src/nodes/predictions_to_text.cpp
  src/nodes/rnnt_call_limiter.cpp
)
set(MODEL_TEST_LIBS model_module node_test_utils)

if(NCNN_ENABLED)
  list(APPEND MODEL_TEST_SRCS src/interpreter/ncnn/tensor.cpp)
endif()

add_giga_edge_test(
  model_tests
  SRCS
    ${MODEL_TEST_SRCS}
  LIBS
    ${MODEL_TEST_LIBS}
)
