cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  common_tests
  SRCS
    src/run_in_parallel.cpp
    src/binary_io.cpp
    src/str_utils.cpp
    src/vector_utils.cpp
    src/syncronizer.cpp
  LIBS
    common_lib
)
