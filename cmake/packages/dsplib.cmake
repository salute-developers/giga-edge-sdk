# The dsplib package for giga_edge_find_sdk_package (cmake/Dependencies.cmake): _targets, _bundled.
find_package(dsplib CONFIG REQUIRED)
set(_targets dsplib::dsplib)
# tts links dsplib by this name (its build is left untouched).
if(NOT TARGET dsplib)
  add_library(dsplib INTERFACE IMPORTED)
  set_target_properties(dsplib PROPERTIES INTERFACE_LINK_LIBRARIES dsplib::dsplib)
  list(APPEND _targets dsplib)
endif()
set(_bundled dsplib::dsplib)
