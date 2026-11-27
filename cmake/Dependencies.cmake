# External dependencies of the SDK. They are installed by tools/dependencies (or in any other way: the project only needs
# CMAKE_PREFIX_PATH).
#
# The core dependencies are looked up here, at the root: an imported target is visible only in the directory of
# find_package and below, while the whole tree needs them, including the nested projects of modules, which expect them
# from the parent. A module declares its own dependencies with register_module(... PACKAGES) in
# giga_edge/modules/<module>/module.cmake, and giga_edge_module looks them up through giga_edge_find_sdk_package only
# when the module is built.
#
# Where a library has a CMake package (its own or one built by a wrapper in tools/dependencies/packages), it is looked
# up explicitly in CONFIG mode; the ones without a package use the Find modules from cmake/find.
# CMAKE_FIND_PACKAGE_PREFER_CONFIG is left alone: it is the consumer's choice (a parent build may set it to OFF).
#
# GIGA_EDGE_BUNDLED_PACKAGES lists the packages that a consumer may merge into its own static archive.

# giga_edge_find_sdk_package(<name>)
# Finds an SDK dependency by its name in tools/dependencies (the name modules declare it by) through
# cmake/packages/<name>.cmake. The package targets are made global: a module looks the package up in its own
# directory, but its neighbors must see the targets too. A repeated call (the same package from another module) does
# nothing.
set(_giga_edge_packages_dir "${CMAKE_CURRENT_LIST_DIR}/packages")
# File names as they are on disk: on a case-insensitive file system EXISTS would also accept "KISSFFT".
file(GLOB _giga_edge_packages RELATIVE "${_giga_edge_packages_dir}" "${_giga_edge_packages_dir}/*.cmake")
function(giga_edge_find_sdk_package NAME)
  get_property(_found GLOBAL PROPERTY GIGA_EDGE_SDK_PACKAGE_${NAME})
  if(_found)
    return()
  endif()

  if(NOT "${NAME}.cmake" IN_LIST _giga_edge_packages)
    message(FATAL_ERROR "giga_edge_find_sdk_package: unknown SDK dependency '${NAME}'")
  endif()
  # The package file finds the package and sets _targets (made global below) and _bundled.
  include("${_giga_edge_packages_dir}/${NAME}.cmake")

  set_target_properties(${_targets} PROPERTIES IMPORTED_GLOBAL TRUE)
  set_property(GLOBAL APPEND PROPERTY GIGA_EDGE_BUNDLED_PACKAGES ${_bundled})
  set_property(GLOBAL PROPERTY GIGA_EDGE_SDK_PACKAGE_${NAME} TRUE)
endfunction()

find_package(AudioFile REQUIRED)

# Catch2 is only for the tests (and their test_utils_lib): a build without tests does not need the package.
if(BUILD_TESTING)
  find_package(Catch2 2 CONFIG REQUIRED)
  # The tests include <catch.hpp>, while the Catch2 v2 package provides <catch2/catch.hpp>: the header directory is
  # looked up next to the include paths of the package (where they are plain paths), otherwise in CMAKE_PREFIX_PATH.
  get_target_property(_catch2_includes Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)
  find_path(CATCH2_SINGLE_INCLUDE_DIR catch.hpp HINTS ${_catch2_includes} PATH_SUFFIXES catch2)
  if(NOT CATCH2_SINGLE_INCLUDE_DIR)
    message(FATAL_ERROR "Catch2 found (${Catch2_DIR}), but it has no catch2/catch.hpp")
  endif()
  set_property(TARGET Catch2::Catch2 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CATCH2_SINGLE_INCLUDE_DIR}")
  unset(_catch2_includes)
  # The package installs Catch.cmake (catch_discover_tests) next to its config.
  list(APPEND CMAKE_MODULE_PATH "${Catch2_DIR}")
endif()

# yaml-cpp may come from the build of a module (register_module ... PROVIDES): a found package would clash with it on
# the target name. With COMPILE_WITH_EXTERNAL_YAMLCPP_PACKAGE the parent provides the target.
module_provided(yaml-cpp _yaml_cpp_provided)
if(NOT _yaml_cpp_provided AND NOT COMPILE_WITH_EXTERNAL_YAMLCPP_PACKAGE)
  find_package(yaml-cpp CONFIG REQUIRED)
  set_property(GLOBAL APPEND PROPERTY GIGA_EDGE_BUNDLED_PACKAGES yaml-cpp)
endif()


if(ARCHIVE_SUPPORT)
  find_package(LibArchive 3 REQUIRED)
endif()


find_package(jsoncpp 1.9 CONFIG REQUIRED)
# jsoncpp packages older than 1.9.5 do not declare JsonCpp::JsonCpp, only jsoncpp_lib/jsoncpp_static.
if(NOT TARGET JsonCpp::JsonCpp)
  add_library(JsonCpp::JsonCpp INTERFACE IMPORTED)
  if(TARGET jsoncpp_static)
    set_target_properties(JsonCpp::JsonCpp PROPERTIES INTERFACE_LINK_LIBRARIES jsoncpp_static)
  else()
    set_target_properties(JsonCpp::JsonCpp PROPERTIES INTERFACE_LINK_LIBRARIES jsoncpp_lib)
  endif()
endif()
# The standard jsoncpp target names, for dependencies that link it by these names.
foreach(_name jsoncpp jsoncpp_lib jsoncpp_static)
  if(NOT TARGET ${_name})
    add_library(${_name} INTERFACE IMPORTED)
    set_target_properties(${_name} PROPERTIES INTERFACE_LINK_LIBRARIES JsonCpp::JsonCpp)
  endif()
endforeach()
