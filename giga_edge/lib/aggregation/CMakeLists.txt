cmake_minimum_required(VERSION 3.13)

giga_edge_lib(aggregation
  SRCS
    src/aggregation.cpp
    src/params.cpp
  DEPS
    enum_lib
)
