cmake_minimum_required(VERSION 3.14)

module_enabled(enhance _enhance_enabled)
if(_enhance_enabled)
  set(CMAKE_PREFIX_PATH
      "${CMAKE_INSTALL_PREFIX};${CMAKE_PREFIX_PATH}"
      CACHE STRING "" FORCE)

  set(BUILD_TESTING_TMP ${BUILD_TESTING})

  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_RESAMPLER OFF CACHE BOOL "" FORCE)
  set(BUILD_TFLITE_ENGINE OFF CACHE BOOL "" FORCE)
  set(BUILD_ONNX_ENGINE OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

  # The library builds its denoiser only if onnxruntime::onnxruntime exists. With ONNXRUNTIME_ENABLED the model module
  # has made this target global, and the package cannot be looked up again next to it: its other targets are not
  # global, and a package refuses to load over a part of its targets. Without it the package is found here (same
  # package set as giga_edge/modules/model/CMakeLists.txt).
  if(NOT TARGET onnxruntime::onnxruntime)
    find_package(Eigen3 CONFIG REQUIRED)
    find_package(boost_mp11 CONFIG REQUIRED)
    find_package(Protobuf CONFIG REQUIRED)
    find_package(onnxruntime CONFIG REQUIRED)
  endif()

  add_subdirectory(
    "${DEPS_SOURCE_DIR}/enhance"
    "${CMAKE_BINARY_DIR}/enhance"
  )

  set(BUILD_TESTING ${BUILD_TESTING_TMP} CACHE BOOL "" FORCE)
endif()

giga_edge_module(enhance
  DEPS
    node_lib
    config_parser_lib
    enum_lib
    giga-edge::enhance
)
