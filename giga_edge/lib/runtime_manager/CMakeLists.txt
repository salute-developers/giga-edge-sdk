cmake_minimum_required(VERSION 3.13)

giga_edge_lib(runtime_manager
  SRCS
    src/capability.cpp
    src/feature_manager.cpp
    src/runtime_manager.cpp
    src/submodel_graph_builder.cpp
  DEPS
    runtime_lib
    runtime_data_lib
    submodel_version_lib
  PRIVATE_DEPS
    enum_lib
)
