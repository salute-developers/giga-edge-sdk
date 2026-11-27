cmake_minimum_required(VERSION 3.16)

# Which SDK dependencies a build needs.
# Usage:
#   cmake [-DPRESET=<name>] [-DMODULES=<a;b>] [-DTARGET_OS=<os>]
#         [-DGIGA_EDGE_ARGS=<CMake arguments for the project>] [-DGIGA_EDGE_ROOT=<repository root>]
#         [-DBUILD_TESTING=OFF]
#         [-DBUILD_TFLITE=ON] [-DBUILD_ONNXRUNTIME=ON]
#         -P tools/scripts/sdk_deps_info.cmake
# Prints the line SDK_DEPS=<dep> <dep> ... to stdout: it is read by build_giga_edge.sh, download_dependencies.sh and
# tools/dependencies (SDK_DEPS). The only place that holds the mapping "option -> dependency" (the dependencies of modules
# are in register_module ... PACKAGES); the same conditions are used by find_package in cmake/Dependencies.cmake and
# giga_edge_module.

if(NOT DEFINED GIGA_EDGE_ROOT)
  get_filename_component(GIGA_EDGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

# The platform, for the conditions in presets.
if(TARGET_OS STREQUAL "android_arm64")
  set(ANDROID ON)
elseif(TARGET_OS STREQUAL "ios_arm64")
  set(IOS ON)
endif()

# The project options that the dependencies depend on: the defaults from the root CMakeLists, overridden by -D from
# GIGA_EDGE_ARGS (--giga-edge-args of build_giga_edge.sh), overridden by the preset: as in the root CMakeLists, the
# preset is included after the options and overrides them.
set(_options ARCHIVE_SUPPORT=ON COMPILE_WITH_EXTERNAL_YAMLCPP_PACKAGE=OFF)
foreach(_opt IN LISTS _options)
  string(REPLACE "=" ";" _opt "${_opt}")
  list(GET _opt 0 _name)
  list(GET _opt 1 _default)
  if(NOT DEFINED ${_name})
    set(${_name} ${_default})
  endif()
  if(" ${GIGA_EDGE_ARGS} " MATCHES " -D${_name}(:[A-Za-z]+)?=([^ ]+) ")
    set(${_name} "${CMAKE_MATCH_2}")
  endif()
endforeach()

set(_requested_modules "${MODULES}")
set(MODULES "")
if(DEFINED PRESET AND NOT PRESET STREQUAL "")
  if(IS_ABSOLUTE "${PRESET}")
    set(_preset_file "${PRESET}")
  else()
    set(_preset_file "${GIGA_EDGE_ROOT}/cmake/presets/${PRESET}.cmake")
  endif()
  include("${_preset_file}" OPTIONAL RESULT_VARIABLE _loaded)
  if(NOT _loaded)
    message(FATAL_ERROR "Unknown preset '${PRESET}': ${_preset_file} not found")
  endif()
endif()
# The modules of the preset are merged with --module: an extra dependency is cheaper than a missing one.
string(REPLACE " " ";" MODULES "${MODULES};${_requested_modules}")
list(REMOVE_ITEM MODULES "")

include("${GIGA_EDGE_ROOT}/cmake/Module.cmake")
include("${GIGA_EDGE_ROOT}/giga_edge/modules/modules.cmake")

# The same rules as in the root CMakeLists: "*" means all modules, empty means the default set of modules,
# then the closure over MODULE_DEPS_*.
if("*" IN_LIST MODULES)
  set(MODULES ${ALL_MODULES})
elseif(NOT MODULES)
  set(MODULES ${GIGA_EDGE_DEFAULT_MODULES})
endif()
set(_changed TRUE)
while(_changed)
  set(_changed FALSE)
  foreach(_m ${MODULES})
    foreach(_dep ${MODULE_DEPS_${_m}})
      if(NOT "${_dep}" IN_LIST MODULES)
        list(APPEND MODULES ${_dep})
        set(_changed TRUE)
      endif()
    endforeach()
  endforeach()
endwhile()

# The core.
set(SDK_DEPS jsoncpp audiofile)
# Catch2 is only for the tests; the tests are enabled by default, as with CTest in the root CMakeLists.
if(NOT DEFINED BUILD_TESTING OR BUILD_TESTING)
  list(APPEND SDK_DEPS catch2)
endif()

# The rest is only for the modules and options that use it. Modules declare their dependencies themselves
# (register_module ... PACKAGES in giga_edge/modules/<module>/module.cmake).
foreach(_m ${MODULES})
  list(APPEND SDK_DEPS ${MODULE_PACKAGES_${_m}})
endforeach()
if(ARCHIVE_SUPPORT)
  list(APPEND SDK_DEPS libarchive)
  # gzip of libarchive uses our own zlib where the system one may be missing: Linux without the dev package, Windows.
  # Without zlib libarchive reads .tar.gz through an external gzip, and parallel loads of model parts hang.
  if(TARGET_OS MATCHES "^(linux|windows)_")
    list(APPEND SDK_DEPS zlib)
  endif()
endif()
# yaml-cpp comes from the build of a module (register_module ... PROVIDES), and with
# COMPILE_WITH_EXTERNAL_YAMLCPP_PACKAGE from the parent.
module_provided(yaml-cpp _yaml_cpp_provided)
if(NOT _yaml_cpp_provided AND NOT COMPILE_WITH_EXTERNAL_YAMLCPP_PACKAGE)
  list(APPEND SDK_DEPS yaml-cpp)
endif()
# The tflite and onnxruntime backends are built with the same Eigen as the modules (tools/dependencies/CMakeLists.txt).
if(BUILD_TFLITE OR BUILD_ONNXRUNTIME)
  list(APPEND SDK_DEPS eigen)
endif()

list(REMOVE_DUPLICATES SDK_DEPS)
string(REPLACE ";" " " _deps "${SDK_DEPS}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "SDK_DEPS=${_deps}")
