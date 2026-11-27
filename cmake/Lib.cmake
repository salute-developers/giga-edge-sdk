include(CMakeParseArguments)

# Declares the infrastructure library <NAME>_lib from the standard template.
# Called INSIDE giga_edge/lib/<NAME>/CMakeLists.txt. Unlike a module, a lib is always built: it has no stub/, no nodes
# and no register_module.
#   SRCS:         sources and headers.
#   DEPS:         PUBLIC dependencies (linked targets).
#   PRIVATE_DEPS: private dependencies of the implementation.
function(giga_edge_lib NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SRCS;DEPS;PRIVATE_DEPS")

  set(target ${NAME}_lib)

  # A lib without sources (header-only) is declared as an INTERFACE target: it has no compilation of its own, and
  # include/internal/deps are propagated as INTERFACE.
  if(ARG_SRCS)
    add_library(${target} STATIC)
    target_sources(${target} PRIVATE ${ARG_SRCS})
    set(_pub PUBLIC)
    set(_priv PRIVATE)
  else()
    add_library(${target} INTERFACE)
    set(_pub INTERFACE)
    set(_priv INTERFACE)
  endif()

  # contrib/ (vendored) declares its targets before linking.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/CMakeLists.txt")
    add_subdirectory(contrib)
  endif()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(${target} ${_pub}
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    install(DIRECTORY include/giga_edge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()

  # internal/ is visible to dependent components at build time but is not installed into the SDK.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/internal")
    target_include_directories(${target} ${_pub}
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>)
  endif()

  if(ARG_DEPS)
    target_link_libraries(${target} ${_pub} ${ARG_DEPS})
  endif()
  if(ARG_PRIVATE_DEPS)
    target_link_libraries(${target} ${_priv} ${ARG_PRIVATE_DEPS})
  endif()

  if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
  endif()
endfunction()
