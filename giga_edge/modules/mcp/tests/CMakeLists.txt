cmake_minimum_required(VERSION 3.13)

find_package(Threads REQUIRED)

add_library(
  mcp_test_utils
  STATIC
  lib/mcp_test_utils/src/fake_connection.cpp
  lib/mcp_test_utils/src/fake_mcp_service.cpp
)

target_include_directories(mcp_test_utils PUBLIC lib/mcp_test_utils/include)
target_link_libraries(mcp_test_utils PUBLIC mcp_module)

add_giga_edge_test(
  mcp_tests
  SRCS
    src/mcp_client_malformed_responses.cpp
    src/mcp_client_metadata.cpp
    src/mcp_client_timeouts.cpp
    src/mcp_request_cancellation.cpp
    src/mcp_environment.cpp
    src/mcp_coordinator_snapshots.cpp
    src/mcp_coordinator_requests.cpp
    src/mcp_coordinator_subscriptions.cpp
    src/mcp_coordinator_happy_path.cpp
    src/mcp_service_lifecycle.cpp
    src/mcp_service_concurrency.cpp
    src/mcp_service_requests.cpp
  LIBS
    mcp_module
    mcp_test_utils
    Threads::Threads
)

add_giga_edge_test(
  mcp_module_tests
  SRCS
    src/nodes/mcp_action_pipeline.cpp
    src/nodes/mcp_catalog_snapshot.cpp
    src/nodes/mcp_completion_pickup.cpp
    src/nodes/mcp_tool_result.cpp
    src/nodes/mcp_tool_specs.cpp
  LIBS
    mcp_module
    assistant_core_module
    mcp_test_utils
    node_test_utils
    Threads::Threads
)

if(UNIX)
  add_executable(mcp_stdio_test_server src/stdio_test_server.cpp)
  target_link_libraries(mcp_stdio_test_server PRIVATE JsonCpp::JsonCpp)
  target_sources(mcp_tests PRIVATE src/mcp_stdio.cpp lib/mcp_stdio_connection/mcp_stdio_connection.cpp)
  target_include_directories(mcp_tests PRIVATE lib/mcp_stdio_connection)
  giga_edge_test_tool(mcp_tests mcp_stdio_test_server MCP_STDIO_TEST_SERVER)
endif()
