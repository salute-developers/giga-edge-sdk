cmake_minimum_required(VERSION 3.14)

giga_edge_module(common
  DEPS
    node_lib
    logical_lib
    math_lib
    common_lib
    config_parser_lib
    enum_lib
    aggregation_lib
    compare_lib
    token_dictionary_lib
    padding_lib
    sliding_window_lib
)
