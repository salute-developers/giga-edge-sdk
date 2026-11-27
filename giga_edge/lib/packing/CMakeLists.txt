cmake_minimum_required(VERSION 3.13)

option(FORCE_ARM_NEON "Force use of ARM NEON code paths" OFF)

giga_edge_lib(packing
  SRCS
    src/5bit.cpp
)

if(FORCE_ARM_NEON)
  set_property(SOURCE src/5bit.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS GIGA_EDGE_PACKING_FORCE_ARM_NEON=1)
endif()

