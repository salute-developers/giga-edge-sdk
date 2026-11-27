cmake_minimum_required(VERSION 3.13)

giga_edge_lib(node
  SRCS
    src/inputs_data.cpp
    src/loop_scheduler.cpp
    src/node_output.cpp
    src/node_params.cpp
    src/node_registry.cpp
    src/node.cpp
    src/outputs_data.cpp
  DEPS
    common_lib
    config_parser_lib
    submodel_version_lib
    data_reader_lib
    statistics_lib
    enum_lib
    environment_lib
)
