cmake_minimum_required(VERSION 3.13)

foreach(MODULE ${ALL_MODULES})
  add_subdirectory(${MODULE})
endforeach()
