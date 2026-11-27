cmake_minimum_required(VERSION 3.14)

giga_edge_module(mcp
  DEPS
    assistant_core_module
    node_lib
    config_parser_lib
    JsonCpp::JsonCpp
    threadpool_lib
    environment_lib
    runtime_manager_lib
  STUB_DEPS
    runtime_manager_lib
)
