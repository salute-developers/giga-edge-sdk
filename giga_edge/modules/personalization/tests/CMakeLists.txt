cmake_minimum_required(VERSION 3.13)

set(PERSONALIZATION_TEST_SRCS persona.cpp)
set(PERSONALIZATION_TEST_LIBS personalization_module node_test_utils)
# The character on a whole graph needs the request nodes from assistant_core.
if("assistant_core" IN_LIST MODULES)
  list(APPEND PERSONALIZATION_TEST_SRCS graph.cpp)
  list(APPEND PERSONALIZATION_TEST_LIBS assistant_core_module runtime_lib)
endif()

add_giga_edge_test(personalization_tests SRCS ${PERSONALIZATION_TEST_SRCS} LIBS ${PERSONALIZATION_TEST_LIBS})
