# Modules are the directories giga_edge/modules/<name>/ with a CMakeLists.txt. A module declares its dependencies,
# packages and other properties itself in <name>/module.cmake (register_module, cmake/Module.cmake); without this file
# the module is registered without properties. Paths are relative to this file: it is also included by `cmake -P`
# scripts (tools/scripts), where PROJECT_SOURCE_DIR is empty.
set(ALL_MODULES "")
set(GIGA_EDGE_DEFAULT_MODULES "")

# A new module directory changes the mtime of this directory, so the configuration is rerun.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB _module_lists RELATIVE "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/*/CMakeLists.txt")
list(SORT _module_lists)
foreach(_module_list IN LISTS _module_lists)
  get_filename_component(_module "${_module_list}" DIRECTORY)
  if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/${_module}/module.cmake")
    set(_expected ${ALL_MODULES} ${_module})
    include("${CMAKE_CURRENT_LIST_DIR}/${_module}/module.cmake")
    if(NOT "${ALL_MODULES}" STREQUAL "${_expected}")
      message(FATAL_ERROR "${_module}/module.cmake must call register_module(${_module}) exactly once")
    endif()
  else()
    register_module(${_module})
  endif()
endforeach()

# AFTER: the module is placed after all the named ones; a single pass is correct as long as the targets have no AFTER
# of their own. A missing target is an error.
foreach(_module IN LISTS ALL_MODULES)
  if(MODULE_AFTER_${_module})
    list(FIND ALL_MODULES ${_module} _at)
    list(REMOVE_AT ALL_MODULES ${_at})
    foreach(_before IN LISTS MODULE_AFTER_${_module})
      if(MODULE_AFTER_${_before})
        message(FATAL_ERROR "${_module}: AFTER ${_before}, which has its own AFTER or is ${_module} itself (no chains)")
      endif()
      list(FIND ALL_MODULES ${_before} _index)
      if(_index EQUAL -1)
        message(FATAL_ERROR "${_module}: AFTER ${_before}: no such module in the tree")
      endif()
      if(_index GREATER_EQUAL _at)
        math(EXPR _at "${_index} + 1")
      endif()
    endforeach()
    list(INSERT ALL_MODULES ${_at} ${_module})
  endif()
endforeach()
