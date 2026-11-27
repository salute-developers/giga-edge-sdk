cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  logical_tests
  SRCS
    src/logical_op.cpp
    src/logical_expression.cpp
  LIBS
    logical_lib
)