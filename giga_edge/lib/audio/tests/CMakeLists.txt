cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  audio_tests
  SRCS
    src/audio.cpp
  LIBS
    audio_lib
)