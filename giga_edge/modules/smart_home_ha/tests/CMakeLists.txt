add_giga_edge_test(smart_home_ha_tests
  SRCS vacuum.cpp colour.cpp catalog.cpp actions.cpp copy.cpp copy_graph.cpp devices.cpp nodes.cpp integration.cpp live_house.cpp scenarios.cpp
    refusal.cpp native_colour_vacuum.cpp
  LIBS smart_home_ha_module node_test_utils mcp_test_utils runtime_lib)

giga_edge_test_source_dir(smart_home_ha_tests GIGA_EDGE_TEST_SOURCE_DIR)
