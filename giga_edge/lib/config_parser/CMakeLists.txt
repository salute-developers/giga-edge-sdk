cmake_minimum_required(VERSION 3.13)

giga_edge_lib(config_parser
  SRCS
    src/config_parser.cpp
  DEPS
    common_lib
    yaml-cpp
)
