# Backend vits: neural synthesis on ONNX models of the VITS family with its own text preparation; included by
# tts/CMakeLists.txt.
option(USE_VITS_BACKEND "Build with the VITS TTS backend" OFF)

if(USE_VITS_BACKEND AND NOT ONNXRUNTIME_ENABLED)
  message(FATAL_ERROR "USE_VITS_BACKEND needs ONNX Runtime: set ONNXRUNTIME_ENABLED ON")
endif()

if(USE_VITS_BACKEND)
  message(STATUS "--- Configuring 'vits' TTS backend")

  target_sources(tts_module PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/acoustic/acoustic_model.cpp
    ${CMAKE_CURRENT_LIST_DIR}/acoustic/bert.cpp
    ${CMAKE_CURRENT_LIST_DIR}/acoustic/prosody.cpp
    ${CMAKE_CURRENT_LIST_DIR}/acoustic/question_rules.cpp
    ${CMAKE_CURRENT_LIST_DIR}/acoustic/voice_config.cpp
    ${CMAKE_CURRENT_LIST_DIR}/acoustic/wordpiece.cpp
    ${CMAKE_CURRENT_LIST_DIR}/interpreter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/params.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/agreement.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/english.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/foreign.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/homograph_classifier.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/homograph_model.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/homographs.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/latin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/lexicon.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/marked.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/morph_dict.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/normalizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/normalizer_dates.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/normalizer_numbers.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/normalizer_units.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/numerals.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/sentence_splitter.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/stress.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/stress_dict.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/stress_model.cpp
    ${CMAKE_CURRENT_LIST_DIR}/text/text_model.cpp
  )
  target_link_libraries(tts_module PRIVATE JsonCpp::JsonCpp)

  list(APPEND TTS_BACKENDS vits)

  if(TARGET tts_tests)
    target_sources(tts_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/acoustic/acoustic_model.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/acoustic/prosody.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/acoustic/question_rules.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/acoustic/voice_config.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/acoustic/wordpiece.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/interpreter.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/params.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/agreement.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/english.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/foreign.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/latin.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/morph_dict.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/normalizer.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/numerals.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/sentence_splitter.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/stress.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/text/stress_dict.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/interpreter/vits/utf8.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/src/nodes/tts_streaming_audio_output/vits/node.cpp
    )
  endif()
endif()
