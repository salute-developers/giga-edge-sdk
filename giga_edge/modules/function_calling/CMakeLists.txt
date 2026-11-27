cmake_minimum_required(VERSION 3.14)

giga_edge_module(function_calling
  DEPS
    assistant_core_module
    config_parser_lib
    JsonCpp::JsonCpp
)
