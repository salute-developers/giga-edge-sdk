cmake_minimum_required(VERSION 3.13)

find_package(Threads REQUIRED)

add_library(web_test_utils INTERFACE)
target_include_directories(web_test_utils INTERFACE lib/web_test_utils/include)
target_link_libraries(web_test_utils INTERFACE web_module)

add_giga_edge_test(
  web_module_tests
  SRCS
    src/web_service_lifecycle.cpp
    src/web_coordinator_requests.cpp
  LIBS
    web_module
    web_test_utils
    Threads::Threads
)
