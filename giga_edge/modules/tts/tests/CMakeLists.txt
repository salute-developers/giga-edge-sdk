cmake_minimum_required(VERSION 3.13)

# Backend tests are added by the backend.cmake of each backend.
add_giga_edge_test(
  tts_tests
  SRCS
    src/interpreter/fake/interpreter.cpp
    src/interpreter/registry.cpp
    src/nodes/tts_audio_accumulator.cpp
    src/nodes/tts_playback_timer.cpp
    src/nodes/tts_streaming_audio_output/fake/node.cpp
  LIBS
    tts_module
    model_module
    node_test_utils
    test_utils_lib
    $<$<NOT:$<PLATFORM_ID:Windows>>:dl>

  DATA data
)

target_include_directories(tts_tests PRIVATE include)
