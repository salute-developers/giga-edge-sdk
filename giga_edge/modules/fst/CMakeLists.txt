cmake_minimum_required(VERSION 3.14)

giga_edge_module(fst
  DEPS
    node_lib
    data_reader_lib
    config_parser_lib
  PRIVATE_DEPS
    OpenFst::fst
)
