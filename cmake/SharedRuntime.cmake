# Installation of the SDK as a prebuilt artifact: one shared library libgiga_edge_runtime, the public headers and a
# CMake package.
#
# The static content (all *_lib/*_module, contrib, the model backends) is linked in as a whole, so a consumer only
# needs:
#   find_package(giga_edge_runtime CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE giga_edge::giga_edge_runtime)
#
# Enabled by the COMPILE_SHARED_RUNTIME option; installed with the regular
# `cmake --install <build-dir> --prefix <prefix>`.

function(giga_edge_add_shared_runtime)
  # A SHARED target must have at least one source; all the content of the library comes from the static archives
  # below.
  set(_stub "${CMAKE_CURRENT_BINARY_DIR}/giga_edge_runtime_shared_stub.cpp")
  file(WRITE "${_stub}" "")
  add_library(giga_edge_runtime_shared SHARED "${_stub}")
  set_target_properties(giga_edge_runtime_shared PROPERTIES OUTPUT_NAME giga_edge_runtime)

  # The whole dependency closure comes through the aggregate; the link mode of the SDK components is overridden to
  # whole-archive below.
  target_link_libraries(giga_edge_runtime_shared PRIVATE giga_edge_runtime)

  # The SDK components are linked as a whole (whole-archive): the registration of graph nodes relies on static
  # initializers (REGISTER_NODE) that nothing references by symbol, so regular linking would drop them. The same
  # libraries occur in the closure both directly and transitively, and the only way to link them consistently with
  # one feature is LINK_LIBRARY_OVERRIDE.
  get_property(_giga_edge_libs GLOBAL PROPERTY GIGA_EDGE_LIBS)
  set(_whole_archive_override "WHOLE_ARCHIVE")
  foreach(_lib IN LISTS _giga_edge_libs ITEMS nodes_registration)
    get_target_property(_type ${_lib} TYPE)
    if(_type STREQUAL "STATIC_LIBRARY")
      string(APPEND _whole_archive_override ",${_lib}")
    endif()
  endforeach()
  set_property(TARGET giga_edge_runtime_shared PROPERTY
    LINK_LIBRARY_OVERRIDE "${_whole_archive_override}")

  install(TARGETS giga_edge_runtime_shared LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

  # The public headers of the SDK include jsoncpp (json/value.h): its headers are installed from the found package so
  # that one prefix is enough for the prebuilt SDK. The library itself is already inside libgiga_edge_runtime.
  set(_jsoncpp_hints "")
  foreach(_target jsoncpp_static jsoncpp_lib JsonCpp::JsonCpp)
    if(TARGET ${_target})
      get_target_property(_dirs ${_target} INTERFACE_INCLUDE_DIRECTORIES)
      if(_dirs)
        list(APPEND _jsoncpp_hints ${_dirs})
      endif()
    endif()
  endforeach()
  find_path(GIGA_EDGE_JSONCPP_INCLUDE_DIR json/json.h HINTS ${_jsoncpp_hints})
  if(NOT GIGA_EDGE_JSONCPP_INCLUDE_DIR)
    message(FATAL_ERROR "COMPILE_SHARED_RUNTIME: jsoncpp headers (json/json.h) not found")
  endif()
  install(DIRECTORY "${GIGA_EDGE_JSONCPP_INCLUDE_DIR}/json" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  set(_shared_name "${CMAKE_SHARED_LIBRARY_PREFIX}giga_edge_runtime${CMAKE_SHARED_LIBRARY_SUFFIX}")
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/giga_edge_runtime-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/giga_edge_runtime-config.cmake"
    @ONLY
  )
  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/giga_edge_runtime-config.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/giga_edge_runtime"
  )
endfunction()
