cmake_minimum_required(VERSION 3.14)

giga_edge_module(audio
  DEPS
    node_lib
    chunk_iterator_lib
    dsplib::dsplib
  PRIVATE_DEPS
    audio_lib
    config_parser_lib
)
