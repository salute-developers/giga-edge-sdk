# Cross build for Linux aarch64: the aarch64-linux-gnu-* compilers come from PATH; the sysroot is GIGA_EDGE_SYSROOT if
# set (for example, EL8 for glibc 2.28), otherwise the compiler's own. The file is passed to all dependencies as well.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
if(DEFINED ENV{GIGA_EDGE_SYSROOT})
  set(CMAKE_SYSROOT "$ENV{GIGA_EDGE_SYSROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
