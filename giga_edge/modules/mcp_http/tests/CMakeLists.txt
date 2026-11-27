set(_srcs http_connection.cpp)
set(_libs mcp_http_module)
module_enabled(web_transport_curl _curl)
if(_curl)
  # End to end through libcurl against the transport's local HTTP fixture.
  list(APPEND _srcs http_connection_curl.cpp)
  list(APPEND _libs web_transport_curl_module web_transport_curl_test_utils)
endif()
add_giga_edge_test(mcp_http_tests SRCS ${_srcs} LIBS ${_libs})
