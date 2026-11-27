cmake_minimum_required(VERSION 3.13)

giga_edge_lib(sequence_id_generator
  SRCS
    src/sequence_id_generator.cpp
  DEPS
    common_lib
)
