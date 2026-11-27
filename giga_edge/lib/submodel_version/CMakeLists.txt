cmake_minimum_required(VERSION 3.13)

giga_edge_lib(submodel_version
  SRCS
    src/config_parser.cpp
    src/submodel_version.cpp
  PRIVATE_DEPS
    config_parser_lib
    common_lib
)
