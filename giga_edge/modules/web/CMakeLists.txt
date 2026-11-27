cmake_minimum_required(VERSION 3.14)

giga_edge_module(web
  DEPS
    common_lib
    environment_lib
    threadpool_lib
    config_parser_lib
    runtime_manager_lib
  STUB_DEPS
    runtime_manager_lib
)
