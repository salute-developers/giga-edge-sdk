cmake_minimum_required(VERSION 3.13)

giga_edge_lib(model_layout
  SRCS
    src/model_layout.cpp
  DEPS
    runtime_data_lib
    data_reader_lib
  PRIVATE_DEPS
    JsonCpp::JsonCpp
)
