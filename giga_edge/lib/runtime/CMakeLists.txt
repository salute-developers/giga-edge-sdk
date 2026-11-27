cmake_minimum_required(VERSION 3.13)

giga_edge_lib(runtime
  SRCS
    src/runtime.cpp
    src/params.cpp
    src/statistics_tracker.cpp
    src/submodel.cpp
  DEPS
    common_lib
    node_lib
    config_parser_lib
    submodel_version_lib
    data_reader_lib
    statistics_lib
    enum_lib
    environment_lib
    sequence_id_generator_lib
    threadpool_lib
    version_lib
)
