cmake_minimum_required(VERSION 3.13)

giga_edge_lib(statistics
  SRCS
    src/statistics.cpp
  DEPS
    common_lib
    JsonCpp::JsonCpp
)
