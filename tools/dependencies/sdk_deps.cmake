# The dependencies of the SDK itself. The project finds them through find_package: by the CMake package where one
# exists, by the Find modules from cmake/find otherwise. The list for a build is computed by
# tools/scripts/sdk_deps_info.cmake; the default is all of them.
#
# The wrappers from packages/ are built with BUILD_ALWAYS: ExternalProject does not track changes in SOURCE_DIR, and a
# wrapper is part of the repository. A repeated build is incremental, and an edit of the wrapper's CMakeLists
# reconfigures it by itself. catch2 and yaml-cpp are built the same way: their clone in DEPS_SOURCE_DIR changes with a
# new pin or patch, and ExternalProject would not notice that by itself.
#
# eigen is built not here but in CMakeLists.txt, before tflite and onnxruntime: they depend on it.
set(SDK_DEPS "jsoncpp;audiofile;eigen;libarchive;openfst;dsplib;catch2;kissfft;yaml-cpp" CACHE STRING "SDK dependencies to build")

if("jsoncpp" IN_LIST SDK_DEPS)
    set(JSONCPP_CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DBUILD_SHARED_LIBS=OFF"
        "-DBUILD_STATIC_LIBS=ON"
        "-DBUILD_OBJECT_LIBS=OFF"
        "-DJSONCPP_WITH_TESTS=OFF"
        "-DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF"
        "-DJSONCPP_WITH_PKGCONFIG_SUPPORT=OFF"
    )
    if(TARGET_OS STREQUAL "windows_x64")
        list(APPEND JSONCPP_CMAKE_ARGS "-DJSONCPP_STATIC_WINDOWS_RUNTIME=ON")
    endif()
    ExternalProject_Add(
        dep_jsoncpp
        SOURCE_DIR "${DEPS_SOURCE_DIR}/jsoncpp"
        CMAKE_ARGS ${JSONCPP_CMAKE_ARGS}
    )
endif()

# Header-only: a single header is installed, AudioFile has no install rules of its own.
if("audiofile" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_audiofile
        SOURCE_DIR "${DEPS_SOURCE_DIR}/audiofile"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "${CMAKE_COMMAND}" -E copy "<SOURCE_DIR>/AudioFile.h" "${CMAKE_INSTALL_PREFIX}/include/AudioFile.h"
    )
endif()

# The only codec is zlib (gzip in data_reader); ACL/xattr/LibGCC are needed only for extraction to disk. Every library
# enabled here is a dependency of the static libarchive that cmake/find/FindLibArchive.cmake links.
# Our own zlib (tools/scripts/sdk_deps_info.cmake adds it on Linux and Windows): the wrapper packages/zlib.
if("zlib" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_zlib
        BUILD_ALWAYS ON
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/packages/zlib"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DSOURCE_DIR=${DEPS_SOURCE_DIR}/zlib"
    )
endif()

if("libarchive" IN_LIST SDK_DEPS)
    set(LIBARCHIVE_CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DENABLE_TEST=OFF"
        "-DENABLE_WERROR=OFF"
        "-DENABLE_TAR=OFF"
        "-DENABLE_CPIO=OFF"
        "-DENABLE_CAT=OFF"
        "-DENABLE_ACL=OFF"
        "-DENABLE_XATTR=OFF"
        "-DENABLE_LibGCC=OFF"
    )
    foreach(codec LZ4 ZSTD LZMA LZO BZip2 OPENSSL MBEDTLS NETTLE LIBB2 LIBXML2 EXPAT PCREPOSIX)
        list(APPEND LIBARCHIVE_CMAKE_ARGS "-DENABLE_${codec}=OFF")
    endforeach()
    if("zlib" IN_LIST SDK_DEPS)
        # Our own zlib by explicit paths: a toolchain with a sysroot looks for packages only under it, not in the prefix.
        # The headers come from the sources: the include of the prefix would give libarchive other headers of the prefix
        # too (an iconv.h of GNU libiconv instead of the one of glibc).
        list(APPEND LIBARCHIVE_CMAKE_ARGS
            "-DZLIB_INCLUDE_DIR=${DEPS_SOURCE_DIR}/zlib"
            "-DZLIB_LIBRARY=${CMAKE_INSTALL_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}z${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
        if(WIN32)
            # libarchive runs its ZLIB_WINAPI check, and a Windows build counts as cross-compiling for CMake; our zlib is
            # built without WINAPI.
            list(APPEND LIBARCHIVE_CMAKE_ARGS "-DZLIB_WINAPI_EXITCODE=1")
        endif()
    endif()
    ExternalProject_Add(
        dep_libarchive
        SOURCE_DIR "${DEPS_SOURCE_DIR}/libarchive"
        CMAKE_ARGS ${LIBARCHIVE_CMAKE_ARGS}
    )
    if("zlib" IN_LIST SDK_DEPS)
        # Order only: the wrapper is built every time (BUILD_ALWAYS), DEPENDS would reconfigure libarchive every time.
        add_dependencies(dep_libarchive dep_zlib)
    endif()
endif()

if("openfst" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_openfst
        BUILD_ALWAYS ON
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/packages/openfst"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DSOURCE_DIR=${DEPS_SOURCE_DIR}/openfst"
    )
endif()

# The real_t type is float32: this is how audio and tts expect dsplib.
if("dsplib" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_dsplib
        BUILD_ALWAYS ON
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/packages/dsplib"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DSOURCE_DIR=${DEPS_SOURCE_DIR}/dsplib"
            "-DBUILD_SHARED_LIBS=OFF"
            "-DDSPLIB_USE_FLOAT32=ON"
    )
endif()

if("catch2" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_catch2
        BUILD_ALWAYS ON
        SOURCE_DIR "${DEPS_SOURCE_DIR}/catch2"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DCATCH_BUILD_TESTING=OFF"
            "-DCATCH_INSTALL_DOCS=OFF"
            "-DCATCH_INSTALL_HELPERS=ON"
            "-DCATCH_ENABLE_WERROR=OFF"
    )
endif()

if("kissfft" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_kissfft
        BUILD_ALWAYS ON
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/packages/kissfft"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DSOURCE_DIR=${DEPS_SOURCE_DIR}/kissfft"
    )
endif()

if("yaml-cpp" IN_LIST SDK_DEPS)
    set(YAML_CPP_CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DYAML_CPP_BUILD_TESTS=OFF"
        "-DYAML_CPP_BUILD_TOOLS=OFF"
        "-DYAML_CPP_BUILD_CONTRIB=ON"
        "-DYAML_BUILD_SHARED_LIBS=OFF"
        "-DYAML_CPP_INSTALL=ON"
    )
    if(TARGET_OS STREQUAL "windows_x64")
        list(APPEND YAML_CPP_CMAKE_ARGS "-DYAML_MSVC_SHARED_RT=OFF")
    endif()
    ExternalProject_Add(
        dep_yaml_cpp
        BUILD_ALWAYS ON
        SOURCE_DIR "${DEPS_SOURCE_DIR}/yaml-cpp"
        CMAKE_ARGS ${YAML_CPP_CMAKE_ARGS}
    )
endif()
