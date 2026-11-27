cmake_minimum_required(VERSION 3.14)

giga_edge_module(features
  DEPS
    node_lib
    data_reader_lib
    config_parser_lib
    asr_features
    enum_lib
    chunk_iterator_lib
  PRIVATE_DEPS
    kissfft::kissfft
)

if(features IN_LIST MODULES)
  target_include_directories(features_module PUBLIC contrib)
  set_property(GLOBAL APPEND PROPERTY GIGA_EDGE_MODULE_LIBS asr_features)

  if(APPLE)
    find_library(ACCELERATE_LIBRARIES Accelerate)
    target_link_libraries(features_module PRIVATE ${ACCELERATE_LIBRARIES})
  endif()
endif()
