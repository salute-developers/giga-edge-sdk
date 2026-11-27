cmake_minimum_required(VERSION 3.14)

giga_edge_module(personalization
  DEPS
    runtime_manager_lib
    environment_lib
    config_parser_lib
    node_lib
  PRIVATE_DEPS
    enum_lib
  STUB_DEPS
    runtime_manager_lib
    environment_lib
)
