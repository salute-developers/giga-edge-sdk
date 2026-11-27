cmake_minimum_required(VERSION 3.14)

giga_edge_module(events
  DEPS
    node_lib
    config_parser_lib
  PRIVATE_DEPS
    enum_lib
    spotter_module
)
