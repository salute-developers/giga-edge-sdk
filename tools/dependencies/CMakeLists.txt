cmake_minimum_required(VERSION 3.16)

project(GigaEdgeDependencies)

set(DEPS_SOURCE_DIR "" CACHE PATH "Directory with all dependencies sources")

if(NOT DEPS_SOURCE_DIR OR NOT EXISTS "${DEPS_SOURCE_DIR}")
    message(FATAL_ERROR "DEPS_SOURCE_DIR is not set or does not exist: pass the directory with the sources of the dependencies.")
endif()
if(NOT CMAKE_INSTALL_PREFIX)
    message(FATAL_ERROR "CMAKE_INSTALL_PREFIX is not set: pass the install directory.")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "CMAKE_BUILD_TYPE is not set, using the default: ${CMAKE_BUILD_TYPE}")
endif()

option(BUILD_TFLITE "Build TensorFlow Lite and its dependencies." OFF)
option(BUILD_ONNXRUNTIME "Build ONNXRuntime and its dependencies." OFF)
option(BUILD_LLAMA_CPP "Build llama.cpp library." OFF)
set(LLAMA_CPP_PRESET "" CACHE STRING "Preset for llama.cpp build tuning")
option(LLAMA_CPP_METAL "Build llama.cpp with the Metal backend (macOS and iOS)." OFF)
option(BUILD_NCNN "Build ncnn library." OFF)
option(BUILD_CURL "Build curl (OpenSSL TLS backend; Schannel on Windows)." OFF)

option(TFLITE_ENABLE_XNNPACK "Enable xnnpack for TFLite." OFF)
option(TFLITE_ENABLE_BENCHMARK "Enable google benchmark." OFF)

message(STATUS "Install directory: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Sources of the dependencies: ${DEPS_SOURCE_DIR}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build TFLite: ${BUILD_TFLITE}")
message(STATUS "Build ONNXRuntime: ${BUILD_ONNXRUNTIME}")
message(STATUS "Build llama.cpp: ${BUILD_LLAMA_CPP}")
message(STATUS "Build ncnn: ${BUILD_NCNN}")
message(STATUS "Build curl: ${BUILD_CURL}")

include(ExternalProject)

set(COMMON_DEPS_CMAKE_ARGS
    "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DBUILD_TESTING=OFF"
    "-DCMAKE_CXX_STANDARD=17"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
)

if(CMAKE_OSX_ARCHITECTURES)
    list(APPEND COMMON_DEPS_CMAKE_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND COMMON_DEPS_CMAKE_ARGS "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endif()
if(IOS)
    set(IOS_DEPS_CMAKE_ARGS "-DCMAKE_SYSTEM_NAME=iOS" "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
    list(APPEND COMMON_DEPS_CMAKE_ARGS ${IOS_DEPS_CMAKE_ARGS})
endif()

if(TARGET_OS STREQUAL "windows_x64")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(WINDOWS_MSVC_RUNTIME "MultiThreadedDebug")
    else()
        set(WINDOWS_MSVC_RUNTIME "MultiThreaded")
    endif()
    set(WINDOWS_TARGET_ARGS
        "-DCMAKE_SYSTEM_NAME=Windows"
        "-DCMAKE_SYSTEM_PROCESSOR=AMD64"
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${WINDOWS_MSVC_RUNTIME}"
    )
    set(WINDOWS_ABSL_ARGS "-DABSL_MSVC_STATIC_RUNTIME=ON")
    set(WINDOWS_ONNX_ARGS "-DONNX_USE_MSVC_STATIC_RUNTIME=ON")
    list(APPEND COMMON_DEPS_CMAKE_ARGS ${WINDOWS_TARGET_ARGS})
endif()

if(ANDROID)
    list(APPEND COMMON_DEPS_CMAKE_ARGS
        "-DANDROID_ABI=${ANDROID_ABI}"
        "-DANDROID_PLATFORM=${ANDROID_PLATFORM}"
        "-DANDROID_STL=${ANDROID_STL}"
        "-DANDROID_NDK=${ANDROID_NDK}"
    )
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
endif()

# Common parameters for autotools (configure/make).
set(AUTOTOOLS_ENV_CMD "")

if(ANDROID)
    if(NOT ANDROID_PLATFORM_LEVEL)
        set(ANDROID_PLATFORM_LEVEL 31)
    endif()
    
    set(AUTOTOOLS_ENV_CMD
        env
        "CC=${CMAKE_C_COMPILER}"
        "CXX=${CMAKE_CXX_COMPILER}"
        "AR=${CMAKE_AR}"
        "RANLIB=${CMAKE_RANLIB}"
        "CFLAGS=${CMAKE_C_FLAGS} --target=aarch64-linux-android${ANDROID_PLATFORM_LEVEL} -Wno-incompatible-function-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion -Wno-format -Wno-error"
        "CXXFLAGS=${CMAKE_CXX_FLAGS} --target=aarch64-linux-android${ANDROID_PLATFORM_LEVEL}"
        "CPPFLAGS=-I${CMAKE_INSTALL_PREFIX}/include"
        "LDFLAGS=-L${CMAKE_INSTALL_PREFIX}/lib"
        "PKG_CONFIG_PATH=${CMAKE_INSTALL_PREFIX}/lib/pkgconfig"
    )
    set(AUTOTOOLS_HOST_FLAG "--host=aarch64-linux-android")
    set(AUTOTOOLS_DISABLE_SHARED "--disable-shared" "--enable-static" "--with-pic")
elseif(TARGET_OS STREQUAL "linux_aarch64")
    set(AUTOTOOLS_ENV_CMD
        env
        "CC=aarch64-linux-gnu-gcc"
        "CXX=aarch64-linux-gnu-g++"
        "AR=aarch64-linux-gnu-ar"
        "RANLIB=aarch64-linux-gnu-ranlib"
        "CFLAGS=${CMAKE_C_FLAGS} -fPIC -Wno-incompatible-function-pointer-types -Wno-int-conversion"
        "CXXFLAGS=${CMAKE_CXX_FLAGS} -fPIC"
        "CPPFLAGS=-I${CMAKE_INSTALL_PREFIX}/include"
        "LDFLAGS=-L${CMAKE_INSTALL_PREFIX}/lib"
        "PKG_CONFIG_PATH=${CMAKE_INSTALL_PREFIX}/lib/pkgconfig"
    )
    set(AUTOTOOLS_HOST_FLAG "--host=aarch64-linux-gnu")
    set(AUTOTOOLS_DISABLE_SHARED "--disable-shared" "--enable-static" "--with-pic")
else()
    set(AUTOTOOLS_ENV_CMD
        env
        "CFLAGS=${CMAKE_C_FLAGS} -fPIC -Wno-incompatible-function-pointer-types -Wno-int-conversion"
        "CXXFLAGS=${CMAKE_CXX_FLAGS} -fPIC"
        "CPPFLAGS=-I${CMAKE_INSTALL_PREFIX}/include"
        "LDFLAGS=-L${CMAKE_INSTALL_PREFIX}/lib"
        "PKG_CONFIG_PATH=${CMAKE_INSTALL_PREFIX}/lib/pkgconfig"
    )
    set(AUTOTOOLS_HOST_FLAG "")
    set(AUTOTOOLS_DISABLE_SHARED "--disable-shared" "--with-pic")
endif()

# The shared libraries (protobuf, flatbuffers, abseil, cpuinfo) are needed by the heavy backends and by the model/tts
# modules.
option(BUILD_COMMON_DEPS "Build shared third-party libraries (protobuf, flatbuffers, abseil, cpuinfo)." ON)

if(BUILD_COMMON_DEPS)

# Host tools (protoc/flatc).
set(HOST_TOOLS_DIR "${CMAKE_BINARY_DIR}/host_tools_install")
set(HOST_PROTOC_EXE "${HOST_TOOLS_DIR}/bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")
set(HOST_FLATC_EXE "${HOST_TOOLS_DIR}/bin/flatc${CMAKE_EXECUTABLE_SUFFIX}")

ExternalProject_Add(
    dep_protobuf_host
    SOURCE_DIR "${DEPS_SOURCE_DIR}/protobuf"
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${HOST_TOOLS_DIR}"
        "-Dprotobuf_BUILD_TESTS=OFF"
        "-Dprotobuf_BUILD_PROTOC_BINARIES=ON"
        "-DBUILD_SHARED_LIBS=OFF"
        "-Dprotobuf_WITH_ZLIB=OFF"
)

ExternalProject_Add(
    dep_flatbuffers_host
    SOURCE_DIR "${DEPS_SOURCE_DIR}/flatbuffers"
    DEPENDS dep_protobuf_host
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=${HOST_TOOLS_DIR}"
        "-DFLATBUFFERS_BUILD_TESTS=OFF"
        "-DFLATBUFFERS_BUILD_FLATLIB=OFF"
        "-DFLATBUFFERS_INSTALL=ON"
        "-DBUILD_SHARED_LIBS=OFF"
)

ExternalProject_Add(
    dep_flatbuffers
    SOURCE_DIR "${DEPS_SOURCE_DIR}/flatbuffers"
    CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DFLATBUFFERS_BUILD_TESTS=OFF"
        "-DFLATBUFFERS_BUILD_FLATC=OFF"
        "-DFLATBUFFERS_INSTALL=ON"
)

ExternalProject_Add(
    dep_absl
    SOURCE_DIR "${DEPS_SOURCE_DIR}/abseil-cpp"
    CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        ${WINDOWS_ABSL_ARGS}
        "-DABSL_ENABLE_INSTALL=ON"
        "-DABSL_PROPAGATE_CXX_STD=ON"
)

ExternalProject_Add(
    dep_cpuinfo
    SOURCE_DIR "${DEPS_SOURCE_DIR}/cpuinfo"
    CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DCPUINFO_BUILD_TOOLS=OFF"
        "-DCPUINFO_BUILD_UNIT_TESTS=OFF"
        "-DCPUINFO_BUILD_MOCK_TESTS=OFF"
        "-DCPUINFO_BUILD_BENCHMARKS=OFF"
)

ExternalProject_Add(
    dep_protobuf
    SOURCE_DIR "${DEPS_SOURCE_DIR}/protobuf"
    DEPENDS dep_absl dep_protobuf_host
    CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}"
        "-Dprotobuf_BUILD_TESTS=OFF"
        "-Dprotobuf_BUILD_PROTOC_BINARIES=OFF"
        "-DProtobuf_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"
)

endif() # BUILD_COMMON_DEPS

# On Windows curl uses Schannel: OpenSSL under MSVC needs nmake and a native Perl, which Git Bash does not have.
if(BUILD_CURL AND NOT WIN32)

    if(IOS)
        set(OPENSSL_TARGET "ios64-xcrun")
        set(OPENSSL_EXTRA_CONFIGURE "")
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            set(OPENSSL_EXTRA_CONFIGURE "-mios-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
        set(OPENSSL_ENV_CMD env)
    elseif(ANDROID)
        # Host OS for the NDK
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
            set(NDK_HOST_TAG "linux-x86_64")
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
            set(NDK_HOST_TAG "darwin-x86_64")
        else()
            message(FATAL_ERROR "Unsupported host OS for Android NDK build: ${CMAKE_HOST_SYSTEM_NAME}")
        endif()

        set(NDK_TOOLCHAIN_BIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/${NDK_HOST_TAG}/bin")
        set(OPENSSL_TARGET "linux-aarch64")
        set(OPENSSL_EXTRA_CONFIGURE "-D__ANDROID_API__=${ANDROID_PLATFORM_LEVEL}")
        set(OPENSSL_ENV_CMD
            env
            "PATH=${NDK_TOOLCHAIN_BIN}:$ENV{PATH}"
            "CC=${NDK_TOOLCHAIN_BIN}/clang --target=aarch64-linux-android${ANDROID_PLATFORM_LEVEL}"
            "CXX=${NDK_TOOLCHAIN_BIN}/clang++ --target=aarch64-linux-android${ANDROID_PLATFORM_LEVEL}"
            "AR=${NDK_TOOLCHAIN_BIN}/llvm-ar"
            "RANLIB=${NDK_TOOLCHAIN_BIN}/llvm-ranlib"
            "ANDROID_NDK=${ANDROID_NDK}"
        )
    elseif(TARGET_OS STREQUAL "linux_aarch64")
        set(OPENSSL_TARGET "linux-aarch64")
        set(OPENSSL_EXTRA_CONFIGURE "")
        set(OPENSSL_ENV_CMD
            env
            "CC=aarch64-linux-gnu-gcc"
            "CXX=aarch64-linux-gnu-g++"
            "AR=aarch64-linux-gnu-ar"
            "RANLIB=aarch64-linux-gnu-ranlib"
            "CFLAGS=${CMAKE_C_FLAGS} -fPIC"
        )
    elseif(TARGET_OS STREQUAL "macos_arm64")
        set(OPENSSL_TARGET "darwin64-arm64")
        set(OPENSSL_EXTRA_CONFIGURE "")
        set(OPENSSL_ENV_CMD
            env
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "CFLAGS=${CMAKE_C_FLAGS} -fPIC"
        )
    elseif(TARGET_OS STREQUAL "macos_x64")
        set(OPENSSL_TARGET "darwin64-x86_64")
        set(OPENSSL_EXTRA_CONFIGURE "")
        set(OPENSSL_ENV_CMD
            env
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "CFLAGS=${CMAKE_C_FLAGS} -fPIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        )
    else()
        # linux_x86_64 and other host builds
        set(OPENSSL_TARGET "linux-x86_64")
        set(OPENSSL_EXTRA_CONFIGURE "")
        set(OPENSSL_ENV_CMD
            env
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}"
            "CFLAGS=${CMAKE_C_FLAGS} -fPIC"
        )
    endif()

    # Direct CLT clang needs the SDK for both compilation and linking.
    if(TARGET_OS STREQUAL "macos_arm64" OR TARGET_OS STREQUAL "macos_x64")
        set(OPENSSL_SDKROOT "${CMAKE_OSX_SYSROOT}")
        if(NOT IS_ABSOLUTE "${OPENSSL_SDKROOT}")
            set(OPENSSL_SDK_ARGS "")
            if(OPENSSL_SDKROOT)
                set(OPENSSL_SDK_ARGS --sdk "${OPENSSL_SDKROOT}")
            endif()
            execute_process(COMMAND xcrun ${OPENSSL_SDK_ARGS} --show-sdk-path
                OUTPUT_VARIABLE OPENSSL_SDKROOT OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE OPENSSL_SDK_RESULT ERROR_QUIET)
            if(NOT OPENSSL_SDK_RESULT EQUAL 0)
                set(OPENSSL_SDKROOT "")
            endif()
        endif()
        if(OPENSSL_SDKROOT)
            list(APPEND OPENSSL_ENV_CMD "SDKROOT=${OPENSSL_SDKROOT}")
        endif()
    endif()

    ExternalProject_Add(
        dep_openssl
        SOURCE_DIR "${DEPS_SOURCE_DIR}/openssl-3.6.0"
        BUILD_IN_SOURCE 1

        CONFIGURE_COMMAND ${OPENSSL_ENV_CMD} ./Configure ${OPENSSL_TARGET}
            --prefix=${CMAKE_INSTALL_PREFIX}
            --libdir=lib
            --openssldir=${CMAKE_INSTALL_PREFIX}
            no-shared
            no-tests
            no-unit-test
            no-engine
            no-module
            ${OPENSSL_EXTRA_CONFIGURE}

        BUILD_COMMAND ${OPENSSL_ENV_CMD} make -j8

        INSTALL_COMMAND ${OPENSSL_ENV_CMD} make install_sw
    )
endif()

if(BUILD_CURL)
    set(CURL_CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}"
        "-DBUILD_SHARED_LIBS=OFF"
        "-DBUILD_STATIC_LIBS=ON"
        "-DBUILD_CURL_EXE=OFF"
        "-DHTTP_ONLY=ON"
        "-DCURL_DISABLE_LDAP=ON"
        # A blocking DNS would stall the curl_multi_perform loop.
        "-DENABLE_THREADED_RESOLVER=ON"
        "-DCURL_USE_LIBSSH2=OFF"
        "-DUSE_NGHTTP2=OFF"
        "-DUSE_LIBIDN2=OFF"
        "-DCURL_USE_LIBPSL=OFF"
        "-DCURL_ZLIB=OFF"
        "-DCURL_BROTLI=OFF"
        "-DCURL_ZSTD=OFF"
    )
    if(WIN32)
        list(APPEND CURL_CMAKE_ARGS "-DCURL_USE_OPENSSL=OFF" "-DCURL_USE_SCHANNEL=ON")
        set(CURL_TLS_DEP "")
    else()
        list(APPEND CURL_CMAKE_ARGS
            "-DOPENSSL_ROOT_DIR=${CMAKE_INSTALL_PREFIX}"
            "-DCURL_USE_OPENSSL=ON"
            # Lets libcurl verify with the default OpenSSL paths when no CA is given: on the device these are
            # the environment variables SSL_CERT_FILE/SSL_CERT_DIR (openssldir points to the prefix of the build
            # machine and does not exist on the target).
            "-DCURL_CA_FALLBACK=ON"
        )
        set(CURL_TLS_DEP dep_openssl)
    endif()
    if(ANDROID OR IOS OR TARGET_OS STREQUAL "linux_aarch64")
        # The toolchains of the NDK, iOS and linux_aarch64 (a sysroot) look for packages only in CMAKE_FIND_ROOT_PATH.
        list(APPEND CURL_CMAKE_ARGS
            "-DCMAKE_FIND_ROOT_PATH=${CMAKE_INSTALL_PREFIX}"
            "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH"
            "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH"
            "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH"
        )
    endif()

    ExternalProject_Add(
        dep_curl
        SOURCE_DIR "${DEPS_SOURCE_DIR}/curl"
        DEPENDS ${CURL_TLS_DEP}
        CMAKE_ARGS ${CURL_CMAKE_ARGS}
    )

    if(ANDROID OR TARGET_OS STREQUAL "linux_aarch64")
        message(WARNING
            "curl built for a cross target without a CA bundle: https verification on the "
            "device fails unless the app passes CURLOPT_CAINFO "
            "or sets SSL_CERT_FILE/SSL_CERT_DIR"
        )
    endif()
endif()

# One Eigen for everything: tflite, onnxruntime and the SDK modules (features, tts) are built with the same version.
# With two versions in one prefix the headers of the one installed last would overwrite the first, and in a binary
# with both backends the same Eigen templates would be defined differently (ODR). Declared here, not in
# sdk_deps.cmake: tflite and onnxruntime below depend on it.
if(BUILD_TFLITE OR BUILD_ONNXRUNTIME OR NOT DEFINED SDK_DEPS OR "eigen" IN_LIST SDK_DEPS)
    ExternalProject_Add(
        dep_eigen
        SOURCE_DIR "${DEPS_SOURCE_DIR}/eigen"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DEIGEN_BUILD_TESTING=OFF"
            "-DEIGEN_BUILD_DOC=OFF"
            "-DEIGEN_BUILD_DEMOS=OFF"
            "-DEIGEN_BUILD_BLAS=OFF"
            "-DEIGEN_BUILD_LAPACK=OFF"
            "-DEIGEN_BUILD_PKGCONFIG=OFF"
            "-DEIGEN_BUILD_CMAKE_PACKAGE=ON"
    )
endif()

if(BUILD_TFLITE)
    # tflite 2.20 links protobuf in tools/benchmark but looks for it in the sibling core/example: this needs
    # CMAKE_FIND_PACKAGE_TARGETS_GLOBAL (below), which CMake < 3.24 silently ignores.
    if(CMAKE_VERSION VERSION_LESS 3.24)
        message(FATAL_ERROR "Building tflite needs CMake >= 3.24, found ${CMAKE_VERSION}")
    endif()
    # XNNPACK (build_giga_edge.sh --tflite-xnnpack) is downloaded by a separate step:
    # download_dependencies.sh --xnnpack.
    if(TFLITE_ENABLE_XNNPACK)
        foreach(_xnn_dep xnnpack pthreadpool fxdiv)
            if(NOT EXISTS "${DEPS_SOURCE_DIR}/${_xnn_dep}/CMakeLists.txt")
                message(FATAL_ERROR "TFLITE_ENABLE_XNNPACK: no ${DEPS_SOURCE_DIR}/${_xnn_dep} — "
                    "download it: tools/scripts/download_dependencies.sh --xnnpack")
            endif()
        endforeach()
    endif()
    ExternalProject_Add(
        dep_farmhash
        SOURCE_DIR "${DEPS_SOURCE_DIR}/farmhash"
        CMAKE_ARGS ${COMMON_DEPS_CMAKE_ARGS}
    )
    ExternalProject_Add(
        dep_fft2d
        SOURCE_DIR "${DEPS_SOURCE_DIR}/fft2d"
        CMAKE_ARGS ${COMMON_DEPS_CMAKE_ARGS}
    )
    ExternalProject_Add(
        dep_gemmlowp
        # The CMake project of gemmlowp is in contrib/: it installs the gemmlowp::gemmlowp package that tflite expects.
        SOURCE_DIR "${DEPS_SOURCE_DIR}/gemmlowp/contrib"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
    )
    ExternalProject_Add(
        dep_neon2sse
        SOURCE_DIR "${DEPS_SOURCE_DIR}/neon2sse"
        CMAKE_ARGS ${COMMON_DEPS_CMAKE_ARGS}
    )
    ExternalProject_Add(
        dep_ruy
        SOURCE_DIR "${DEPS_SOURCE_DIR}/ruy"
        DEPENDS dep_cpuinfo
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DRUY_FIND_CPUINFO=ON"
            "-DRUY_MINIMAL_BUILD=ON"
            "-Dcpuinfo_DIR=${CMAKE_INSTALL_PREFIX}/share/cpuinfo"
    )

    set(TFLITE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON"
        "-DTFLITE_ENABLE_INSTALL=ON"
        "-DTFLITE_ENABLE_NNAPI=OFF"
        "-DTFLITE_ENABLE_GPU=OFF"
        # RUY off: with it the test scores differ
        "-Dabsl_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/absl"
        "-Dcpuinfo_DIR=${CMAKE_INSTALL_PREFIX}/share/cpuinfo"
        "-DEigen3_DIR=${CMAKE_INSTALL_PREFIX}/share/eigen3/cmake"
        "-Dfarmhash_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/farmhash"
        "-Dfft2d_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/fft2d"
        "-Dgemmlowp_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/gemmlowp"
        "-Druy_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/ruy"
        "-DNEON_2_SSE_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/NEON_2_SSE"
        "-DFlatBuffers_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/flatbuffers"
        "-DFLATBUFFERS_FLATC_EXECUTABLE=${HOST_FLATC_EXE}"
        # In a cross build tflite 2.20 does not configure without TFLITE_HOST_TOOLS_DIR; FLATC_BIN keeps find_program
        # from picking some flatc from PATH before ours.
        "-DTFLITE_HOST_TOOLS_DIR=${HOST_TOOLS_DIR}"
        "-DFLATC_BIN=${HOST_FLATC_EXE}"
        # tflite gets ml_dtypes (since TF 2.15) through FetchContent: from our copy, not from GitHub.
        "-DFETCHCONTENT_SOURCE_DIR_ML_DTYPES=${DEPS_SOURCE_DIR}/ml_dtypes"
        "-DFETCHCONTENT_SOURCE_DIR_FP16_HEADERS=${DEPS_SOURCE_DIR}/fp16"
        # tools/benchmark and core/example (tflite always adds them) generate protobufs.
        "-DProtobuf_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/protobuf"
        "-DProtobuf_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"
        # core/example looks for protobuf, but the sibling tools/benchmark links it: found packages are made global.
        "-DCMAKE_FIND_PACKAGE_TARGETS_GLOBAL=ON"
    )
    if(TFLITE_ENABLE_XNNPACK)
        list(APPEND TFLITE_ARGS
            "-DTFLITE_ENABLE_XNNPACK=ON"
            # XNNPACK is built inside tflite and installed into its package (patch tensorflow/0006); the sources are our
            # clones, otherwise tflite and XNNPACK download them from GitHub at configure time.
            "-DFETCHCONTENT_SOURCE_DIR_XNNPACK=${DEPS_SOURCE_DIR}/xnnpack"
            "-DPTHREADPOOL_SOURCE_DIR=${DEPS_SOURCE_DIR}/pthreadpool"
            "-DFXDIV_SOURCE_DIR=${DEPS_SOURCE_DIR}/fxdiv"
            "-DFP16_SOURCE_DIR=${DEPS_SOURCE_DIR}/fp16"
            # cpuinfo is our installed package (the patch maps the cpuinfo target to cpuinfo::cpuinfo); the directory is
            # given so that XNNPACK does not download its own and takes the headers of the same version.
            "-DCPUINFO_SOURCE_DIR=${DEPS_SOURCE_DIR}/cpuinfo"
            # KleidiAI in XNNPACK is only for int8 GEMM (our models are float32), and llama.cpp on arm64 has its own, of
            # a different version: two copies in one binary are an ODR violation.
            "-DXNNPACK_ENABLE_KLEIDIAI=OFF"
            # The full set of microkernels is needed only by the XNNPACK tests and benchmarks.
            "-DXNNPACK_BUILD_ALL_MICROKERNELS=OFF"
            # The delegate is applied only by the SDK, on the model option use_xnnpack (patch tensorflow/0006).
            "-DTFLITE_XNNPACK_DEFAULT_DELEGATE=OFF"
        )
        if(NOT TARGET_OS STREQUAL "windows_x64")
            # Otherwise find_library(LIBM m) writes an absolute path to libm into the installed package (on macOS, into
            # the SDK); Windows has no libm: XNNPACK does not find or link it there.
            list(APPEND TFLITE_ARGS "-DLIBM:STRING=m")
        endif()
    else()
        list(APPEND TFLITE_ARGS "-DTFLITE_ENABLE_XNNPACK=OFF")
    endif()

    ExternalProject_Add(
        tensorflow-lite
        SOURCE_DIR "${DEPS_SOURCE_DIR}/tensorflow/tensorflow/lite"
        DEPENDS dep_absl dep_flatbuffers dep_flatbuffers_host dep_cpuinfo dep_ruy dep_eigen dep_protobuf dep_protobuf_host
                dep_farmhash dep_fft2d dep_gemmlowp dep_neon2sse
        CMAKE_ARGS ${TFLITE_ARGS}
    )
endif()

if(BUILD_ONNXRUNTIME)
    message(STATUS "--- Configuring the ONNXRuntime dependencies ---")

    set(MP11_DEPS "")
    set(ONNX_DEPS
        dep_cpuinfo
        dep_re2
        dep_mp11
        dep_safe_int
        dep_protobuf
        dep_date
        dep_nlohmann_json
        dep_gsl
        dep_onnx
        dep_flatbuffers
        dep_flatbuffers_host
    )

    list(APPEND ONNX_DEPS dep_eigen)

    if(TARGET_OS STREQUAL "windows_x64")
        ExternalProject_Add(
            dep_wil
            SOURCE_DIR "${DEPS_SOURCE_DIR}/wil"
            CMAKE_ARGS
                ${COMMON_DEPS_CMAKE_ARGS}
                "-DWIL_BUILD_PACKAGING=OFF"
                "-DWIL_BUILD_TESTS=OFF"
        )
        list(APPEND ONNX_DEPS dep_wil)
    endif()

    ExternalProject_Add(
        dep_re2
        SOURCE_DIR "${DEPS_SOURCE_DIR}/re2"
        DEPENDS dep_absl
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DRE2_BUILD_TESTING=OFF"
            "-Dabsl_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/absl"
    )
    ExternalProject_Add(
        dep_mp11
        SOURCE_DIR "${DEPS_SOURCE_DIR}/mp11"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DBOOST_ENABLE_CMAKE=ON"
            DEPENDS ${MP11_DEPS}
    )
    ExternalProject_Add(
        dep_safe_int
        SOURCE_DIR "${DEPS_SOURCE_DIR}/SafeInt"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        # Headers only: without .git, Test/ and Archive/.
        INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_INSTALL_PREFIX}/include/SafeInt"
            COMMAND ${CMAKE_COMMAND} -E copy
                <SOURCE_DIR>/SafeInt.hpp <SOURCE_DIR>/safe_math.h <SOURCE_DIR>/safe_math_impl.h
                "${CMAKE_INSTALL_PREFIX}/include/SafeInt"
    )
    ExternalProject_Add(
        dep_date
        SOURCE_DIR "${DEPS_SOURCE_DIR}/date"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DENABLE_DATE_TESTING=OFF"
            "-DUSE_SYSTEM_TZ_DB=ON"
            "-DBUILD_SHARED_LIBS=OFF"
    )
    ExternalProject_Add(
        dep_nlohmann_json
        SOURCE_DIR "${DEPS_SOURCE_DIR}/json"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DJSON_BuildTests=OFF"
            "-DJSON_Install=ON"
    )
    if(WIN32)
        set(ONNX_INSTALL_COMMAND
            ${CMAKE_COMMAND} --build . --target install --config ${CMAKE_BUILD_TYPE}
            COMMAND ${CMAKE_COMMAND} -DCONFIG=${CMAKE_INSTALL_PREFIX}/lib/cmake/ONNX/ONNXConfig.cmake
                -DFROM=${HOST_TOOLS_DIR} -DTO=${CMAKE_INSTALL_PREFIX} -P ${CMAKE_CURRENT_SOURCE_DIR}/patch_onnx_config.cmake
        )
    else()
        set(ONNX_INSTALL_COMMAND sh -c "$(MAKE) install && echo 'Patching ONNXConfig.cmake...' && sed -i.bak 's|${HOST_TOOLS_DIR}|${CMAKE_INSTALL_PREFIX}|g' ${CMAKE_INSTALL_PREFIX}/lib/cmake/ONNX/ONNXConfig.cmake")
    endif()
    ExternalProject_Add(
        dep_gsl
        SOURCE_DIR "${DEPS_SOURCE_DIR}/GSL"
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            "-DGSL_TEST=OFF"
    )

    ExternalProject_Add(
        dep_onnx
        SOURCE_DIR "${DEPS_SOURCE_DIR}/onnx"
        DEPENDS dep_protobuf dep_protobuf_host
        CMAKE_ARGS
            ${COMMON_DEPS_CMAKE_ARGS}
            ${WINDOWS_ONNX_ARGS}
            "-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}"
            "-DONNX_BUILD_TESTS=OFF"
            "-DONNX_DISABLE_STATIC_REGISTRATION=ON"
            "-DONNX_USE_LITE_PROTO=OFF"
            "-DProtobuf_INCLUDE_DIR=${CMAKE_INSTALL_PREFIX}/include"
            "-DProtobuf_LIBRARY=${CMAKE_INSTALL_PREFIX}/lib/libprotobuf.a"
            "-DProtobuf_LIBRARIES=${CMAKE_INSTALL_PREFIX}/lib/libprotobuf.a"
            "-DProtobuf_LITE_LIBRARY=${CMAKE_INSTALL_PREFIX}/lib/libprotobuf-lite.a"
            "-DProtobuf_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"
        INSTALL_COMMAND ${ONNX_INSTALL_COMMAND}
    )
    
    set(ONNXRUNTIME_ARGS
        "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
        "-DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        
        "-DCMAKE_FIND_ROOT_PATH=${CMAKE_INSTALL_PREFIX}"
        "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH"
        "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH"
        "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH"

        "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
        "-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS"
        
        "-Dabsl_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/absl"
        "-Dre2_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/re2"
        "-Ddate_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/date"
        "-Dnlohmann_json_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/nlohmann_json"
        "-DMicrosoft.GSL_DIR=${CMAKE_INSTALL_PREFIX}/share/cmake/Microsoft.GSL"
        "-Dprotobuf_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/protobuf"
        "-DProtobuf_INCLUDE_DIR=${CMAKE_INSTALL_PREFIX}/include"
        "-DProtobuf_LIBRARY=${CMAKE_INSTALL_PREFIX}/lib/libprotobuf.a"
        "-DProtobuf_LITE_LIBRARY=${CMAKE_INSTALL_PREFIX}/lib/libprotobuf-lite.a"
        
        "-Donnxruntime_USE_PREBUILT_PROTOBUF=ON"
        "-DProtobuf_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"
        "-DONNX_CUSTOM_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"

        "-DFETCHCONTENT_SOURCE_DIR_XNNPACK=${DEPS_SOURCE_DIR}/xnnpack"
        "-DFETCHCONTENT_SOURCE_DIR_CPUINFO=${DEPS_SOURCE_DIR}/cpuinfo"
        "-DFETCHCONTENT_SOURCE_DIR_PTHREADPOOL=${DEPS_SOURCE_DIR}/pthreadpool"
        "-DFETCHCONTENT_SOURCE_DIR_FXDIV=${DEPS_SOURCE_DIR}/fxdiv"
        "-DFETCHCONTENT_SOURCE_DIR_FP16=${DEPS_SOURCE_DIR}/fp16"
        "-DFETCHCONTENT_SOURCE_DIR_PSIMD=${DEPS_SOURCE_DIR}/psimd"
        "-DFETCHCONTENT_SOURCE_DIR_EIGEN3=${DEPS_SOURCE_DIR}/eigen"
        "-DFETCHCONTENT_SOURCE_DIR_MP11=${DEPS_SOURCE_DIR}/mp11"
        # The copy in install/: the path ends up in the installed onnxruntimeConfig.cmake, and a path into downloads
        # would break the consumer.
        "-Dsafeint_SOURCE_DIR=${CMAKE_INSTALL_PREFIX}/include/SafeInt"

        "-Donnxruntime_BUILD_SHARED_LIB=OFF"
        "-Donnxruntime_RUN_ONNX_TESTS=OFF"
        "-Donnxruntime_BUILD_UNIT_TESTS=OFF"
        "-Donnxruntime_BUILD_BENCHMARKS=OFF"
        "-Donnxruntime_ENABLE_PYTHON=OFF"
        "-Donnxruntime_BUILD_CSHARP=OFF"
        "-Donnxruntime_BUILD_JAVA=OFF"
        "-Donnxruntime_BUILD_NODEJS=OFF"
        "-Donnxruntime_BUILD_OBJC=OFF"
        "-Donnxruntime_BUILD_APPLE_FRAMEWORK=OFF"
        "-Donnxruntime_BUILD_WEBASSEMBLY_STATIC_LIB=OFF"
        "-DONNX_DISABLE_STATIC_REGISTRATION=ON"
        "-Donnxruntime_USE_XNNPACK=OFF"
        "-Donnxruntime_USE_NNAPI_BUILTIN=OFF"
        "-Donnxruntime_USE_VCPKG=OFF"
        "-Donnxruntime_USE_DNNL=OFF"
        "-Donnxruntime_USE_TENSORRT=OFF"
        "-Donnxruntime_USE_CUDA=OFF"
        "-Donnxruntime_USE_ROCM=OFF"
        "-Donnxruntime_USE_OPENMP=OFF"
        "-Donnxruntime_USE_ACL=OFF"
        "-Donnxruntime_USE_ARMNN=OFF"
        "-Donnxruntime_ENABLE_MICROSOFT_INTERNAL=OFF"
        "-Donnxruntime_DISABLE_CONTRIB_OPS=OFF"
        "-Donnxruntime_DISABLE_ML_OPS=OFF"
        "-Donnxruntime_DISABLE_RTTI=OFF"
        "-Donnxruntime_DISABLE_EXCEPTIONS=OFF"
    )
    set(ONNXRUNTIME_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    if(TARGET_OS STREQUAL "windows_x64")
        list(APPEND ONNXRUNTIME_ARGS ${WINDOWS_TARGET_ARGS}
            "-Dwil_DIR=${CMAKE_INSTALL_PREFIX}/share/cmake/WIL")
        # C4709 is a false positive of MSVC 14.50 in custom_ops.cc, and ORT is built with /WX.
        string(APPEND ONNXRUNTIME_CXX_FLAGS " /wd4702 /wd4709")
    endif()

    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND ONNXRUNTIME_ARGS "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            list(APPEND ONNXRUNTIME_ARGS "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
    endif()

    if(IOS)
        list(APPEND ONNXRUNTIME_ARGS ${IOS_DEPS_CMAKE_ARGS} "-Donnxruntime_CROSS_COMPILING=ON")
    endif()
    if(TARGET_OS STREQUAL "linux_aarch64")
        list(APPEND ONNXRUNTIME_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" "-Donnxruntime_CROSS_COMPILING=ON")
    endif()

    if(ANDROID)
        list(APPEND ONNXRUNTIME_ARGS
            "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
            "-DCMAKE_SYSTEM_NAME=Android"
            "-DANDROID_ABI=${ANDROID_ABI}"
            "-DANDROID_PLATFORM=${ANDROID_PLATFORM}"
            "-DANDROID_STL=${ANDROID_STL}"
            "-Donnxruntime_CROSS_COMPILING=ON"
            # Only the build flags (the cache), without the Android ones: the toolchain adds those itself.
            "-DCMAKE_C_FLAGS=$CACHE{CMAKE_C_FLAGS}"
            "-DCMAKE_CXX_FLAGS=$CACHE{CMAKE_CXX_FLAGS}"
        )
    else()
        list(APPEND ONNXRUNTIME_ARGS
             "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
             "-DCMAKE_CXX_FLAGS=${ONNXRUNTIME_CXX_FLAGS}"
        )
    endif()

    ExternalProject_Add(
        onnxruntime
        SOURCE_DIR "${DEPS_SOURCE_DIR}/onnxruntime/cmake"
        DEPENDS ${ONNX_DEPS}
        CMAKE_ARGS ${ONNXRUNTIME_ARGS}
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target install
    )
endif()

if(BUILD_LLAMA_CPP)
    set(LLAMA_CPP_CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        "-DBUILD_SHARED_LIBS=OFF"
        "-DLLAMA_BUILD_EXAMPLES=OFF"
        "-DLLAMA_BUILD_TESTS=OFF"
        "-DLLAMA_BUILD_SERVER=OFF"
        "-DLLAMA_BUILD_TOOLS=OFF"
        "-DLLAMA_BUILD_APP=OFF"
        "-DLLAMA_BUILD_COMMON=OFF"
        "-DLLAMA_OPENSSL=OFF"
        "-DGGML_CUDA=OFF"
        "-DGGML_HIP=OFF"
        "-DGGML_VULKAN=OFF"
        "-DGGML_OPENCL=OFF"
        "-DGGML_OPENMP=OFF"
    )
    if(TARGET_OS STREQUAL "windows_x64")
        list(APPEND LLAMA_CPP_CMAKE_ARGS ${WINDOWS_TARGET_ARGS})
    endif()

    if(CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
        list(APPEND LLAMA_CPP_CMAKE_ARGS "-DGGML_NATIVE=OFF")
    endif()

    if(LLAMA_CPP_PRESET STREQUAL "cortex-a55")
        list(APPEND LLAMA_CPP_CMAKE_ARGS
            "-DGGML_NATIVE=OFF"
            "-DGGML_CPU_KLEIDIAI=OFF"
            "-DGGML_LLAMAFILE=OFF"
            "-DGGML_CPU_ARM_ARCH=armv8.2-a+fp16+dotprod"
            "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON"
            "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -O3 -mcpu=cortex-a55+nocrypto -fomit-frame-pointer -fno-plt"
            "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -O3 -mcpu=cortex-a55+nocrypto -fomit-frame-pointer -fno-plt"
        )
    elseif(LLAMA_CPP_PRESET STREQUAL "cuda")
        list(REMOVE_ITEM LLAMA_CPP_CMAKE_ARGS "-DGGML_CUDA=OFF")
        list(APPEND LLAMA_CPP_CMAKE_ARGS
            "-DGGML_CUDA=ON"
        )
    elseif(LLAMA_CPP_PRESET STREQUAL "portable")
        # Distributed builds: no native tuning for the build machine (on a Mac, the M1 set); Intel Macs get AVX2.
        list(APPEND LLAMA_CPP_CMAKE_ARGS "-DGGML_NATIVE=OFF")
        if(TARGET_OS STREQUAL "macos_x64")
            list(APPEND LLAMA_CPP_CMAKE_ARGS
                "-DGGML_AVX=ON" "-DGGML_AVX2=ON" "-DGGML_FMA=ON" "-DGGML_F16C=ON" "-DGGML_AVX512=OFF"
            )
        elseif(TARGET_OS STREQUAL "android_arm64" OR TARGET_OS STREQUAL "linux_aarch64" OR TARGET_OS STREQUAL "ios_arm64")
            # The baseline is ARMv8.0 (Cortex-A53/A73 without dotprod), and KleidiAI picks the dotprod, I8MM and SME
            # multiplications at run time: on new cores it is 4-6 % slower than a dotprod build, but one build fits all.
            list(APPEND LLAMA_CPP_CMAKE_ARGS
                "-DGGML_CPU_ARM_ARCH=armv8-a" "-DGGML_CPU_KLEIDIAI=ON"
                "-DFETCHCONTENT_SOURCE_DIR_KLEIDIAI=${DEPS_SOURCE_DIR}/kleidiai"
            )
        endif()
    elseif(LLAMA_CPP_PRESET)
        message(FATAL_ERROR "Unknown LLAMA_CPP_PRESET='${LLAMA_CPP_PRESET}'. Supported values: cortex-a55, cuda, portable")
    endif()

    if(TARGET_OS STREQUAL "macos_arm64" OR TARGET_OS STREQUAL "macos_x64")
        list(APPEND LLAMA_CPP_CMAKE_ARGS "-DGGML_METAL=${LLAMA_CPP_METAL}")
    elseif(TARGET_OS STREQUAL "ios_arm64")
        list(APPEND LLAMA_CPP_CMAKE_ARGS "-DGGML_NATIVE=OFF" "-DGGML_METAL=${LLAMA_CPP_METAL}" "-DGGML_METAL_EMBED_LIBRARY=${LLAMA_CPP_METAL}")
    elseif(TARGET_OS STREQUAL "linux_x86_64" OR TARGET_OS STREQUAL "windows_x64")
        list(APPEND LLAMA_CPP_CMAKE_ARGS
            "-DGGML_NATIVE=OFF" "-DGGML_AVX=ON" "-DGGML_AVX2=ON" "-DGGML_FMA=ON" "-DGGML_F16C=ON"
            "-DGGML_AVX512=OFF"
        )
    endif()

    ExternalProject_Add(
        dep_llama_cpp
        SOURCE_DIR "${DEPS_SOURCE_DIR}/llama.cpp"
        CMAKE_ARGS ${LLAMA_CPP_CMAKE_ARGS}
    )
endif()

if(BUILD_NCNN)
    set(NCNN_CMAKE_ARGS
        ${COMMON_DEPS_CMAKE_ARGS}
        # On Android and iOS ncnn turns off exceptions and RTTI by default with PUBLIC flags: they would reach the SDK
        # modules.
        "-DNCNN_DISABLE_EXCEPTION=OFF" "-DNCNN_DISABLE_RTTI=OFF"
        # The library only: the ncnn converters find the protobuf of the prefix without protoc when it is installed
        # before ncnn.
        "-DNCNN_BUILD_TOOLS=OFF" "-DNCNN_BUILD_EXAMPLES=OFF" "-DNCNN_BUILD_BENCHMARK=OFF"
    )
    ExternalProject_Add(
        dep_ncnn
        SOURCE_DIR ${DEPS_SOURCE_DIR}/ncnn
        CMAKE_ARGS ${NCNN_CMAKE_ARGS}
    )
endif()

include(sdk_deps.cmake)
