cmake_minimum_required(VERSION 3.13)

find_package(Threads REQUIRED)

add_giga_edge_test(
  assistant_core_module_tests
  SRCS
    src/prompt_template.cpp
    src/tool_call_filter.cpp
    src/input_nodes.cpp
    src/assistant_session.cpp
    src/dialogue_request_builder.cpp
    src/request_profile.cpp
    src/dialogue_state_update.cpp
    src/stream_nodes.cpp
    src/correlation.cpp
    src/utils.cpp
    src/turn_trace.cpp
  LIBS
    assistant_core_module
    node_test_utils
)

giga_edge_test_source_dir(assistant_core_module_tests GIGA_EDGE_TEST_SOURCE_DIR)

# The graphs of example/offline_assistant also run nodes of the common, spotter and events modules; the tests on the
# sound of the reply also need the tts module and the persona node of the core (the personalization module).
module_enabled(common _assistant_core_tests_common)
module_enabled(spotter _assistant_core_tests_spotter)
module_enabled(tts _assistant_core_tests_tts)
module_enabled(personalization _assistant_core_tests_personalization)
if(_assistant_core_tests_common AND _assistant_core_tests_spotter)
  target_sources(assistant_core_module_tests PRIVATE src/demo_graphs.cpp)
  target_link_libraries(assistant_core_module_tests common_module spotter_module events_module runtime_lib)
  if(_assistant_core_tests_tts AND _assistant_core_tests_personalization)
    target_sources(assistant_core_module_tests PRIVATE src/demo_graphs_reply.cpp)
    target_link_libraries(assistant_core_module_tests tts_module personalization_module)
  endif()
endif()
