cmake_minimum_required(VERSION 3.13)

# all.h: the registration headers of all modules in the tree, nodes first, then environments (the same list as for
# the stubs in cmake/Module.cmake: a disabled module has an empty file at this path). It is not installed.
set(GIGA_EDGE_REGISTRATION_INCLUDES "")
set(_modules ${ALL_MODULES})
list(SORT _modules)
foreach(_header IN ITEMS nodes.h environment.h)
  foreach(_module IN LISTS _modules)
    if(EXISTS "${PROJECT_SOURCE_DIR}/giga_edge/modules/${_module}/internal/giga_edge/modules/${_module}/${_header}")
      string(APPEND GIGA_EDGE_REGISTRATION_INCLUDES "#include \"giga_edge/modules/${_module}/${_header}\"\n")
    endif()
  endforeach()
endforeach()
string(STRIP "${GIGA_EDGE_REGISTRATION_INCLUDES}" GIGA_EDGE_REGISTRATION_INCLUDES)
configure_file(all.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/giga_edge/nodes/all.h" @ONLY)

add_library(nodes_registration STATIC src/registration.cpp)

target_include_directories(nodes_registration PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>)
install(DIRECTORY include/giga_edge DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

get_property(_giga_edge_libs GLOBAL PROPERTY GIGA_EDGE_LIBS)
target_link_libraries(nodes_registration PUBLIC ${_giga_edge_libs})
