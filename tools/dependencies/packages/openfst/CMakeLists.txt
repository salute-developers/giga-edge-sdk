# Installation of openfst: the CMake description of the library (src/CMakeLists.txt) installs nothing. Only libfst is
# built (the project does not need fstscript); the package description is in cmake/find/FindOpenFst.cmake of the
# project.
cmake_minimum_required(VERSION 3.13)
project(openfst_package CXX)

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is not set: the path to the openfst sources")
endif()

add_subdirectory("${SOURCE_DIR}/src" openfst EXCLUDE_FROM_ALL)
add_custom_target(openfst_libs ALL DEPENDS fst)

if(MSVC)
    # The CMake description of openfst passes GCC flags unconditionally (-fno-exceptions, -W*), MSVC fails on them
    # (D8021). openfst needs an unsigned char: its MSVC equivalent is /J. There is no dlopen registration on Windows;
    # NOMINMAX: otherwise windows.h breaks std::max with the max macro.
    get_target_property(_fst_opts fst COMPILE_OPTIONS)
    list(FILTER _fst_opts EXCLUDE REGEX "^-[fW]")
    set_target_properties(fst PROPERTIES COMPILE_OPTIONS "${_fst_opts};/J")
    target_compile_definitions(fst PRIVATE FST_NO_DYNAMIC_LINKING NOMINMAX)
endif()

install(TARGETS fst ARCHIVE DESTINATION lib)
install(DIRECTORY "${SOURCE_DIR}/src/include/fst" DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.inc")
