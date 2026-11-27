# Backend pre_synthesized: ready-made wav files found by text from the metadata; included by tts/CMakeLists.txt.
option(USE_PRE_SYNTHESIZED_BACKEND "Build with Pre-synthesized TTS backend" ON)

if(USE_PRE_SYNTHESIZED_BACKEND)
  message(STATUS "--- Configuring 'pre_synthesized' TTS backend")

  target_sources(tts_module PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/interpreter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/params.cpp
  )

  list(APPEND TTS_BACKENDS pre_synthesized)

  if(TARGET tts_tests)
    target_sources(tts_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/pre_synthesized/factory.cpp)
  endif()
endif()
