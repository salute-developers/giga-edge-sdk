cmake_minimum_required(VERSION 3.13)

add_giga_edge_test(
  audio_module_tests
  SRCS
    src/nodes/audio_gather.cpp
    src/nodes/audio_segmenter.cpp
    src/nodes/audio_iterator.cpp
    src/nodes/combine_channels.cpp
    src/nodes/deinterleave.cpp
    src/nodes/extract_channel.cpp
    src/nodes/extract_channels.cpp
    src/nodes/interleave.cpp
    src/nodes/resampler.cpp
  LIBS
    audio_module
    node_test_utils
)