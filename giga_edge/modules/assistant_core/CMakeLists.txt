cmake_minimum_required(VERSION 3.14)

giga_edge_module(assistant_core
  DEPS
    node_lib
    config_parser_lib
    JsonCpp::JsonCpp
)
