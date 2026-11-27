cmake_minimum_required(VERSION 3.13)

giga_edge_lib(test_utils
  SRCS
    src/matchers.cpp
)
target_link_libraries(test_utils_lib PRIVATE Catch2::Catch2)
