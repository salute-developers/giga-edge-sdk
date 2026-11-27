cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  enum_tests
  SRCS
    src/enum.cpp
  LIBS
    enum_lib
)