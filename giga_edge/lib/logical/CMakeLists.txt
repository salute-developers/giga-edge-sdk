cmake_minimum_required(VERSION 3.13)

giga_edge_lib(logical
  SRCS
    src/logical_expression.cpp
    src/logical_op.cpp
  PRIVATE_DEPS
    enum_lib
)
