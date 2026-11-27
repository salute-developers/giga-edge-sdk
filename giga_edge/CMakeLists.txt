cmake_minimum_required(VERSION 3.13)

include_directories(
        contrib
)

add_subdirectory(lib)

set(LIBS ${CMAKE_DL_LIBS} JsonCpp::JsonCpp)

set(GIGA_EDGE_LIBS
  aggregation_lib
  audio_lib
  common_lib
  compare_lib
  config_parser_lib
  data_reader_lib
  enum_lib
  environment_lib
  logical_lib
  math_lib
  runtime_lib
  runtime_data_lib
  runtime_manager_lib
  node_lib
  packing_lib
  padding_lib
  sliding_window_lib
  token_dictionary_lib
  threadpool_lib
  sequence_id_generator_lib
  statistics_lib
  submodel_version_lib
  version_lib
  chunk_iterator_lib
)

foreach(MODULE ${ALL_MODULES})
  list(APPEND GIGA_EDGE_LIBS ${MODULE}_module)
endforeach()

set_property(GLOBAL PROPERTY GIGA_EDGE_LIBS ${GIGA_EDGE_LIBS})

set(LIBS
  ${LIBS}
  ${GIGA_EDGE_LIBS}
)

set(LIBS ${LIBS} yaml-cpp)

add_library(giga_edge_runtime INTERFACE)

IF(APPLE)
  find_library(ACCELERATE_LIBRARIES Accelerate)
  find_library(FOUNDATION_LIBRARIES Foundation)
  find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
  find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
  set(LIBS ${LIBS} ${ACCELERATE_LIBRARIES} ${FOUNDATION_LIBRARIES} ${COREAUDIO_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK})
ELSE()
  find_package(Threads REQUIRED)
  set(LIBS ${LIBS} ${CMAKE_THREAD_LIBS_INIT})
ENDIF()

add_subdirectory(modules)
add_subdirectory(nodes)

target_link_libraries(giga_edge_runtime INTERFACE ${LIBS} nodes_registration)

if(COMPILE_SHARED_RUNTIME)
    giga_edge_add_shared_runtime()
endif()
