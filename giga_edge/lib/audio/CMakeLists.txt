cmake_minimum_required(VERSION 3.13)

giga_edge_lib(audio
  SRCS
    src/audio.cpp
  DEPS
    AudioFile::AudioFile
)
