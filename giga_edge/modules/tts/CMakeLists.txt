cmake_minimum_required(VERSION 3.14)

giga_edge_module(tts
  SRCS
    src/interpreter/registry.cpp
    src/nodes/tts_audio_accumulator/node.cpp
    src/nodes/tts_audio_accumulator/params.cpp
    src/nodes/tts_playback_timer/node.cpp
    src/nodes/tts_playback_timer/params.cpp
    src/nodes/tts_streaming_audio_output/node.cpp
    src/nodes/tts_streaming_audio_output/params.cpp
  DEPS
    data_reader_lib
    node_lib
    config_parser_lib
    audio_lib
    enum_lib
    environment_lib
    model_module
)

module_enabled(tts _tts_enabled)
if(_tts_enabled)
  # A synthesis backend is a directory src/interpreter/backends/<name>/: its backend.cmake adds sources, dependencies
  # and tests, and an enabled backend appends its name to TTS_BACKENDS. It is registered by REGISTER_TTS_BACKEND in
  # internal/.../backends/<name>/interpreter.h, which the registry includes through the generated backends.h.
  set(TTS_BACKENDS "")
  file(GLOB _tts_backend_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/interpreter/backends/*/backend.cmake")
  foreach(_tts_backend_file IN LISTS _tts_backend_files)
    include("${_tts_backend_file}")
  endforeach()
  if(NOT TTS_BACKENDS)
    message(FATAL_ERROR "No TTS backend enabled: turn on at least one (options in src/interpreter/backends/*/backend.cmake)")
  endif()

  set(_tts_backend_includes "")
  foreach(_tts_backend IN LISTS TTS_BACKENDS)
    string(APPEND _tts_backend_includes
      "#include \"giga_edge/modules/tts/interpreter/backends/${_tts_backend}/interpreter.h\"\n")
  endforeach()
  file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/giga_edge/modules/tts/interpreter/backends.h"
    CONTENT "#pragma once\n\n${_tts_backend_includes}"
  )
  target_include_directories(tts_module PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/include")
endif()
