cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  math_tests
  SRCS
    src/math_expression.cpp
  LIBS
    math_lib
)
