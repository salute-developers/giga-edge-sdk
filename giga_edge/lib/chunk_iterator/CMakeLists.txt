cmake_minimum_required(VERSION 3.13)

giga_edge_lib(chunk_iterator
  SRCS
    src/chunk_iterator.cpp
    src/params.cpp
  PRIVATE_DEPS
    config_parser_lib
)
