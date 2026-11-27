# zlib for gzip in libarchive: inflate/deflate only, without the gz* files (libarchive does not call them, and they need
# unistd.h). Our own CMake description: the CMake of zlib renames zconf.h in the download tree in an out-of-source build.
cmake_minimum_required(VERSION 3.13)
project(zlib_package C)

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is not set: the path to the zlib sources")
endif()

set(ZLIB_SOURCES adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
list(TRANSFORM ZLIB_SOURCES PREPEND "${SOURCE_DIR}/")
list(TRANSFORM ZLIB_SOURCES APPEND ".c")
add_library(zlib STATIC ${ZLIB_SOURCES})
target_include_directories(zlib PRIVATE "${SOURCE_DIR}")
if(MSVC)
    target_compile_definitions(zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()
# libz.a / z.lib: the name FindZLIB finds it by. Hidden symbols: in the shared libraries of the SDK they are not visible
# outside and do not clash with another zlib of the process.
set_target_properties(zlib PROPERTIES OUTPUT_NAME z C_VISIBILITY_PRESET hidden)

install(TARGETS zlib ARCHIVE DESTINATION lib)
install(FILES "${SOURCE_DIR}/zlib.h" "${SOURCE_DIR}/zconf.h" DESTINATION include)
