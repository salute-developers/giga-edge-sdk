include(CMakeParseArguments)

# register_module(<name> [DEFAULT] [DEPENDS <module>...] [PACKAGES <package>...] [PROVIDES <package>...]
#                 [AFTER <module>...])
# Called from giga_edge/modules/<name>/module.cmake (see giga_edge/modules/modules.cmake).
#   DEFAULT:  the module is in the default set (empty MODULES).
#   DEPENDS:  the modules that are enabled together with this one.
#   PACKAGES: the external dependencies of the module by their tools/dependencies names (openfst, kissfft, ...): the
#             superbuild installs them (tools/scripts/sdk_deps_info.cmake) and giga_edge_module looks them up only when
#             the module is built (see giga_edge_find_sdk_package).
#   PROVIDES: the packages that the module brings with its own build: they are neither looked up nor installed.
#   AFTER:    the modules of the tree that this one follows in ALL_MODULES (the configuration and link order),
#             without enabling them: for a module that needs another module's target at configuration time, or when
#             the position of library copies in the link line matters. A rare key, no chains: the target module must
#             be in the tree (except the ones left out of the open-source distribution) and must have no AFTER
#             itself. Without AFTER the order is alphabetical; DEPENDS does not change it.
macro(register_module NAME)
  cmake_parse_arguments(RM "DEFAULT" "" "DEPENDS;PACKAGES;PROVIDES;AFTER" ${ARGN})
  list(APPEND ALL_MODULES ${NAME})
  if(RM_DEFAULT)
    list(APPEND GIGA_EDGE_DEFAULT_MODULES ${NAME})
  endif()
  set(MODULE_DEPS_${NAME} ${RM_DEPENDS})
  set(MODULE_PACKAGES_${NAME} ${RM_PACKAGES})
  set(MODULE_PROVIDES_${NAME} ${RM_PROVIDES})
  set(MODULE_AFTER_${NAME} ${RM_AFTER})
endmacro()

# module_provided(<package> <out_var>): whether the build of some module of the tree brings the package (PROVIDES).
function(module_provided PACKAGE OUT_VAR)
  foreach(_module ${ALL_MODULES})
    if("${PACKAGE}" IN_LIST MODULE_PROVIDES_${_module})
      set(${OUT_VAR} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${OUT_VAR} FALSE PARENT_SCOPE)
endfunction()

function(module_enabled NAME OUT_VAR)
  if("${NAME}" IN_LIST MODULES)
    set(${OUT_VAR} TRUE PARENT_SCOPE)
  else()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
  endif()
endfunction()

# Declares the target <NAME>_module. Called INSIDE giga_edge/modules/<NAME>/CMakeLists.txt and handles every case
# itself:
#   * a header-only module (only include/, no src/SRCS/stub/internal/) is an INTERFACE target that is ALWAYS
#     available (there is nothing to compile or stub); membership in MODULES does not matter;
#   * the module is SELECTED (NAME is in MODULES): a full build from SRCS + include/internal/src + DEPS;
#   * the module is NOT selected: a stub. The public headers (include/) stay, stub/ provides the implementation
#     (not-implemented), heavy DEPS are not pulled in. If the module provides graph nodes
#     (internal/giga_edge/modules/<NAME>/nodes.h), the stub generates an empty nodes.h at the same path so that the
#     graph aggregator compiles without the domain. An internal-only module without stub/*.cpp is stubbed as
#     INTERFACE: there is no code to compile, so no empty placeholder TU is needed.
#   SRCS:         sources and headers of the module (full build).
#   DEPS:         PUBLIC dependencies (full build; INTERFACE for a header-only module).
#   PRIVATE_DEPS: private dependencies of the implementation (full build; not for a header-only module).
#   STUB_DEPS:    PUBLIC dependencies of the stub (besides common_lib) needed by the code in stub/.
function(giga_edge_module NAME)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SRCS;DEPS;PRIVATE_DEPS;STUB_DEPS")

  set(target ${NAME}_module)

  # A header-only module: only public headers (include/), no src/SRCS/stub/internal/. There is nothing to compile or
  # stub, so it is declared as an INTERFACE target that is always available regardless of membership in MODULES (its
  # headers are needed by the public surface of other modules).
  if(NOT ARG_SRCS
      AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src"
      AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stub"
      AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/internal")
    add_library(${target} INTERFACE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
      target_include_directories(${target} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
      install(DIRECTORY include/giga_edge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
    if(ARG_DEPS)
      target_link_libraries(${target} INTERFACE ${ARG_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS OR ARG_STUB_DEPS)
      message(WARNING
        "giga_edge_module(${NAME}): PRIVATE_DEPS/STUB_DEPS do not apply to a header-only INTERFACE module")
    endif()
    if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
      add_subdirectory(tests)
    endif()
    return()
  endif()

  module_enabled(${NAME} _module_enabled)
  if(NOT _module_enabled)
    message("Stubbing module ${NAME}")

    set(_stub_srcs "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stub")
      file(GLOB _stub_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stub/*.cpp")
    endif()

    if(_stub_srcs)
      # There is a public stub (a not-implemented implementation), so a compiled STATIC library is needed.
      add_library(${target} STATIC ${_stub_srcs})
      set(_scope PUBLIC)
    else()
      # An internal-only module (only graph nodes, no public stub/): the stub has no code of its own, so it is
      # declared as an INTERFACE target.
      add_library(${target} INTERFACE)
      set(_scope INTERFACE)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
      target_include_directories(${target} ${_scope}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
      install(DIRECTORY include/giga_edge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()

    # The registration headers of a module (nodes, environments) are included by the generated giga_edge/nodes/all.h
    # (giga_edge/nodes/all.h.in). For a disabled module empty files are put at the same paths so that the aggregators
    # compile without the domain.
    foreach(_registration_header IN ITEMS nodes.h environment.h)
      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/internal/giga_edge/modules/${NAME}/${_registration_header}")
        set(_stub_inc "${CMAKE_CURRENT_BINARY_DIR}/stub-include")
        file(MAKE_DIRECTORY "${_stub_inc}/giga_edge/modules/${NAME}")
        file(TOUCH "${_stub_inc}/giga_edge/modules/${NAME}/${_registration_header}")
        target_include_directories(${target} ${_scope} "${_stub_inc}")
      endif()
    endforeach()

    target_link_libraries(${target} ${_scope} common_lib)
    if(ARG_STUB_DEPS)
      target_link_libraries(${target} ${_scope} ${ARG_STUB_DEPS})
    endif()
    return()
  endif()

  add_library(${target} STATIC)

  # The external packages of the module (register_module ... PACKAGES) come before linking the DEPS that refer to them.
  foreach(_package IN LISTS MODULE_PACKAGES_${NAME})
    giga_edge_find_sdk_package(${_package})
  endforeach()

  if(ARG_SRCS)
    # An explicit SRCS disables the automatic glob: the module decides what to compile itself (needed by modules with
    # conditional sources, e.g. the backends in model, see giga_edge/modules/model/CMakeLists.txt).
    target_sources(${target} PRIVATE ${ARG_SRCS})
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
    file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    target_sources(${target} PRIVATE ${_srcs})
  endif()

  # contrib/ (vendored) declares its targets before linking.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/contrib/CMakeLists.txt")
    add_subdirectory(contrib)
  endif()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_include_directories(${target} PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
    install(DIRECTORY include/giga_edge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()

  # internal/ is visible to dependent modules and the core at build time but is not installed into the SDK.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/internal")
    target_include_directories(${target} PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/internal>)
  endif()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  endif()

  if(ARG_DEPS)
    target_link_libraries(${target} PUBLIC ${ARG_DEPS})
  endif()
  if(ARG_PRIVATE_DEPS)
    target_link_libraries(${target} PRIVATE ${ARG_PRIVATE_DEPS})
  endif()

  if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
  endif()
endfunction()
