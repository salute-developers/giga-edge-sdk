cmake_minimum_required(VERSION 3.13)

giga_edge_lib(padding
  SRCS
    src/padding.cpp
    src/params.cpp
  DEPS
    enum_lib
    config_parser_lib
)
