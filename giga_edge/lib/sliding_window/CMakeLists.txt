cmake_minimum_required(VERSION 3.13)

giga_edge_lib(sliding_window
  SRCS
    src/sliding_window.cpp
  DEPS
    aggregation_lib
)
