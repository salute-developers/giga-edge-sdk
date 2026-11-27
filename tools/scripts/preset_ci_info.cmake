cmake_minimum_required(VERSION 3.16)

# Preset metadata for build_giga_edge.sh.
# Usage:
#   cmake -DPRESET=<name> [-DGIGA_EDGE_ROOT=<repository root>] -P tools/scripts/preset_ci_info.cmake
# Prints the lines MODULES=<modules> and BUILD_FLAGS=<build_giga_edge.sh flags> to stdout. The only place that holds
# the mapping "backend option -> build_giga_edge.sh flag".

if(NOT DEFINED PRESET OR PRESET STREQUAL "")
  message(FATAL_ERROR "PRESET is required: cmake -DPRESET=<name> -P preset_ci_info.cmake")
endif()

if(NOT DEFINED GIGA_EDGE_ROOT)
  get_filename_component(GIGA_EDGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# PRESET is a name from cmake/presets/ or an absolute path to a .cmake file (as in the root CMakeLists).
if(IS_ABSOLUTE "${PRESET}")
  set(_preset_file "${PRESET}")
else()
  set(_preset_file "${GIGA_EDGE_ROOT}/cmake/presets/${PRESET}.cmake")
endif()
include("${_preset_file}" OPTIONAL RESULT_VARIABLE _loaded)
if(NOT _loaded)
  message(FATAL_ERROR "Unknown preset '${PRESET}': ${_preset_file} not found")
endif()

# MODULES: separators are normalized and "*" is expanded (as in the root CMakeLists).
string(REPLACE " " ";" MODULES "${MODULES}")
if("${MODULES}" STREQUAL "*")
  include("${GIGA_EDGE_ROOT}/cmake/Module.cmake")
  include("${GIGA_EDGE_ROOT}/giga_edge/modules/modules.cmake")
  set(MODULES ${ALL_MODULES})
endif()

# A backend not mentioned in the preset is considered disabled: ON/TRUE/1 -> ON, otherwise OFF.
foreach(_opt TFLITE_ENABLED TFLITE_XNNPACK ONNXRUNTIME_ENABLED LLAMA_CPP_ENABLED NCNN_ENABLED)
  if(${_opt})
    set(${_opt} ON)
  else()
    set(${_opt} OFF)
  endif()
endforeach()

# Backend -> build_giga_edge.sh flag.
set(BUILD_FLAGS "")
macro(add_flag COND FLAG)
  if(${COND})
    string(APPEND BUILD_FLAGS " ${FLAG}")
  endif()
endmacro()
add_flag(TFLITE_ENABLED "--build-tflite")
add_flag(TFLITE_XNNPACK "--tflite-xnnpack")
if(TFLITE_XNNPACK AND NOT TFLITE_ENABLED)
  message(FATAL_ERROR "Preset '${PRESET}': TFLITE_XNNPACK requires TFLITE_ENABLED")
endif()
add_flag(ONNXRUNTIME_ENABLED "--build-onnxruntime")
add_flag(LLAMA_CPP_ENABLED "--build-llama-cpp")
add_flag(NCNN_ENABLED "--build-ncnn")

string(STRIP "${BUILD_FLAGS}" BUILD_FLAGS)

# message() writes to stderr, so `cmake -E echo` is used for the machine-readable stdout.
function(emit KEY VALUE)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${KEY}=${VALUE}")
endfunction()

string(REPLACE ";" " " _modules "${MODULES}")
emit(MODULES "${_modules}")
emit(BUILD_FLAGS "${BUILD_FLAGS}")
