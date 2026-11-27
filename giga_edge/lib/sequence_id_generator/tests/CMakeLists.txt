cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  sequence_id_generator_tests
  SRCS
    src/sequence_id_generator.cpp
  LIBS
    sequence_id_generator_lib
)