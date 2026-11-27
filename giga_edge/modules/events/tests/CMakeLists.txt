cmake_minimum_required(VERSION 3.13)


add_library(
  events_test_utils
  STATIC
  lib/events_test_utils/src/events_test_utils.cpp
)

target_include_directories(events_test_utils PUBLIC lib/events_test_utils/include)
target_link_libraries(events_test_utils PUBLIC node_test_utils JsonCpp::JsonCpp spotter_module)

add_giga_edge_test(
  events_tests
  SRCS
    src/nodes/change_event_type.cpp
    src/nodes/filter_events.cpp
    src/nodes/filter_hotword_events.cpp
    src/nodes/gather_events.cpp
    src/top_event.cpp
  LIBS
    events_module
    node_test_utils
    events_test_utils
)
