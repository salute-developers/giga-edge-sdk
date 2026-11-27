cmake_minimum_required(VERSION 3.14)

giga_edge_module(spotter
  DEPS
    node_lib
    config_parser_lib
    synthesis_text_module
    enum_lib
    events_module
    common_lib
)
