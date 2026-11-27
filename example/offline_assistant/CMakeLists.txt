cmake_minimum_required(VERSION 3.16)
project(offline_assistant_demo C CXX)

# Offline assistant demo on Home Assistant: the SDK is built inside the build tree of the example (add_subdirectory)
# with the preset offline_assistant. The SDK looks for the prebuilt external dependencies (onnxruntime, llama.cpp, TensorFlow Lite,
# curl, etc.) through find_package in CMAKE_PREFIX_PATH: the SDK build prepares them (README.md, step 1); by default
# its result on this machine is used (build_macos_arm64/).

set(CMAKE_CXX_STANDARD 17)

get_filename_component(GIGA_EDGE_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(GIGA_EDGE_DEPS_BUILD "${GIGA_EDGE_SDK_ROOT}/build_macos_arm64" CACHE PATH
    "SDK build directory with the built dependencies (install/)")
if(NOT CMAKE_PREFIX_PATH)
  set(CMAKE_PREFIX_PATH "${GIGA_EDGE_DEPS_BUILD}/install")
endif()
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)
if(NOT Protobuf_PROTOC_EXECUTABLE AND EXISTS "${GIGA_EDGE_DEPS_BUILD}/dependencies/host_tools_install/bin/protoc")
  # onnxruntime is built with its own protobuf, so the same protoc is used (host).
  set(Protobuf_PROTOC_EXECUTABLE "${GIGA_EDGE_DEPS_BUILD}/dependencies/host_tools_install/bin/protoc")
  set(protobuf_PROTOC_EXECUTABLE "${Protobuf_PROTOC_EXECUTABLE}")
endif()

set(PRESET offline_assistant CACHE STRING "SDK preset: a name from cmake/presets/ or the absolute path of a preset file")
if(IS_ABSOLUTE "${PRESET}")
  set(_preset_file "${PRESET}")
else()
  set(_preset_file "${GIGA_EDGE_SDK_ROOT}/cmake/presets/${PRESET}.cmake")
endif()
if(NOT EXISTS "${_preset_file}")
  # Without the preset the SDK would be configured with its default modules and backends.
  message(FATAL_ERROR "No preset file ${_preset_file}: pass -DPRESET=<the absolute path of a preset file>")
endif()
set(BUILD_TESTING OFF)
# The parts of the demo model are directories, so reading archives (libarchive) is not needed.
set(ARCHIVE_SUPPORT OFF)
add_subdirectory("${GIGA_EDGE_SDK_ROOT}" giga_edge_sdk)

add_executable(offline_assistant_demo main.cpp bug_report.cpp)
target_link_libraries(
  offline_assistant_demo PRIVATE giga_edge_runtime model_layout_lib mcp_http_module web_transport_curl_module
)

