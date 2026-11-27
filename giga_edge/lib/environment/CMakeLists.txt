cmake_minimum_required(VERSION 3.13)

giga_edge_lib(environment
  SRCS
    src/environment.cpp
    src/params.cpp
  DEPS
    enum_lib
    threadpool_lib
    config_parser_lib
)
