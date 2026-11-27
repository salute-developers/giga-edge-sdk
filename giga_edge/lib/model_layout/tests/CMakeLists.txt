cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  model_layout_tests
  SRCS
    model_layout_test.cpp
  LIBS
    model_layout_lib
    data_reader_lib
)
