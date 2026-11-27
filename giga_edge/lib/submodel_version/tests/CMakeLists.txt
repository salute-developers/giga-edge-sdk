cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  submodel_version_tests
  SRCS
    src/submodel_version.cpp
  LIBS
    submodel_version_lib
)