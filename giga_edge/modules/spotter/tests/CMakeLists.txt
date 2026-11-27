cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  spotter_tests
  SRCS
    src/nodes/event_generator.cpp
    src/nodes/reference.cpp
    src/nodes/events_to_float_activations.cpp
    src/nodes/extract_event_hotword.cpp
    src/nodes/has_activation.cpp
    src/nodes/hotword_sleeping_event_processor.cpp
  LIBS
    spotter_module
    node_test_utils
    events_test_utils
)