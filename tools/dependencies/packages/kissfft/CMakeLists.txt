# CMake package of kissfft: at this commit the library has only a Makefile. It builds the sources the SDK uses (the
# core and the real/nd transforms from tools/) and exports a target (kissfft-config.cmake, kissfft::kissfft). The
# headers, including the ones from tools/, are installed flat into include/kissfft, as in the own CMake build of
# later kissfft versions: consumers include "kissfft/kiss_fft.h", "kissfft/kiss_fftr.h", and the
# #include "kiss_fft.h" of the tools/ headers finds the neighboring file.
cmake_minimum_required(VERSION 3.15)
project(kissfft_package C)

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is not set: the path to the kissfft sources")
endif()

include(GNUInstallDirs)
add_library(kissfft STATIC
    "${SOURCE_DIR}/kiss_fft.c"
    "${SOURCE_DIR}/tools/kfc.c"
    "${SOURCE_DIR}/tools/kiss_fftnd.c"
    "${SOURCE_DIR}/tools/kiss_fftndr.c"
    "${SOURCE_DIR}/tools/kiss_fftr.c")
target_include_directories(kissfft
    PRIVATE "${SOURCE_DIR}" "${SOURCE_DIR}/tools"
    INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

install(TARGETS kissfft EXPORT kissfft-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${SOURCE_DIR}/kiss_fft.h" "${SOURCE_DIR}/_kiss_fft_guts.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kissfft)
install(FILES
    "${SOURCE_DIR}/tools/kfc.h"
    "${SOURCE_DIR}/tools/kiss_fftnd.h"
    "${SOURCE_DIR}/tools/kiss_fftndr.h"
    "${SOURCE_DIR}/tools/kiss_fftr.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kissfft)
install(EXPORT kissfft-targets NAMESPACE kissfft:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kissfft)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kissfft-config.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/kissfft-targets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/kissfft-config.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kissfft)
