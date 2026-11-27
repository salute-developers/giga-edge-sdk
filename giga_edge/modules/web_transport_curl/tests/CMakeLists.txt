cmake_minimum_required(VERSION 3.13)

find_package(Threads REQUIRED)

add_library(web_transport_curl_test_utils INTERFACE)
target_include_directories(web_transport_curl_test_utils INTERFACE
  lib/web_transport_curl_test_utils/include
)
target_link_libraries(web_transport_curl_test_utils INTERFACE web_test_utils)

add_giga_edge_test(
  web_transport_curl_tests
  SRCS
    src/local_http_server_test.cpp
    src/curl_web_transport.cpp
  LIBS
    web_transport_curl_module
    web_transport_curl_test_utils
    web_test_utils
    Threads::Threads
)
