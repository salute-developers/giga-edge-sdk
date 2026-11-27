cmake_minimum_required(VERSION 3.13)

set(RUNTIME_MANAGER_TEST_SRCS
  src/submodel_manager_require_mode.cpp
  src/submodel_manager_setstate.cpp
  src/submodel_manager_parallel_load.cpp
  src/deferred_parts.cpp
  src/environment.cpp
  src/feature_manager.cpp
  src/tar_part_reader.cpp
)

add_giga_edge_test(
  runtime_manager_tests
  SRCS
    ${RUNTIME_MANAGER_TEST_SRCS}
  LIBS
    audio_lib
    runtime_manager_lib
    data_reader_lib
    events_module
    synthesis_text_module
    runtime_test_utils
    giga_edge_runtime
)

# Shared test headers (matchers.h).
target_include_directories(runtime_manager_tests PRIVATE src)
