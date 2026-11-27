# The kissfft package for giga_edge_find_sdk_package (cmake/Dependencies.cmake): _targets, _bundled.
find_package(kissfft CONFIG REQUIRED)
set(_targets kissfft::kissfft)
# asr_features links kissfft by this name (it is left untouched).
if(NOT TARGET kissfft)
  add_library(kissfft INTERFACE IMPORTED)
  set_target_properties(kissfft PROPERTIES INTERFACE_LINK_LIBRARIES kissfft::kissfft)
  list(APPEND _targets kissfft)
endif()
set(_bundled kissfft::kissfft)
