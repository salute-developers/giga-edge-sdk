cmake_minimum_required(VERSION 3.14)

giga_edge_module(audio_io
  PRIVATE_DEPS
    chunk_iterator_lib
    common_lib
)

if(audio_io IN_LIST MODULES AND APPLE)
  find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  target_link_libraries(audio_io_module PRIVATE
    ${COREAUDIO_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK})
endif()
