cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  chunk_iterator_tests
  SRCS
    src/chunk_iterator.cpp
  LIBS
    chunk_iterator_lib
    config_parser_lib
)