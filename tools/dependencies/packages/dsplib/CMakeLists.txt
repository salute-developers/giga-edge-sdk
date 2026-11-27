# CMake package of dsplib: the library installs an archive and headers but no package. It is built as is and the
# target dsplib::dsplib is exported (dsplib-config.cmake).
cmake_minimum_required(VERSION 3.15)
project(dsplib_package CXX)

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is not set: the path to the dsplib sources")
endif()

# dsplib uses CMAKE_INSTALL_INCLUDEDIR in $<INSTALL_INTERFACE> before it includes GNUInstallDirs: without this the
# exported target has an empty include path.
include(GNUInstallDirs)
add_subdirectory("${SOURCE_DIR}" dsplib)
get_directory_property(DSPLIB_VERSION DIRECTORY "${SOURCE_DIR}" DEFINITION dsplib_VERSION)

# dsplib sets C++17 with the global CMAKE_CXX_STANDARD, so the target passes it on to the consumer.
target_compile_features(dsplib INTERFACE cxx_std_17)

install(TARGETS dsplib EXPORT dsplib-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT dsplib-targets NAMESPACE dsplib:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsplib)

include(CMakePackageConfigHelpers)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dsplib-config.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/dsplib-targets.cmake\")\n")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/dsplib-config-version.cmake"
    VERSION ${DSPLIB_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/dsplib-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/dsplib-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dsplib)
