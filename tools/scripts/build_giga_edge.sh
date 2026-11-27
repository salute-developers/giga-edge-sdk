#!/bin/bash
#
# One script to download and build the dependencies and giga_edge itself.

set -ef
set -o pipefail

TARGET_OS=""
BUILD_TYPE="Release"
BUILD_TFLITE=false
TFLITE_XNNPACK=false
BUILD_ONNXRUNTIME=false
BUILD_LLAMA_CPP=false
LLAMA_CPP_PRESET=""
LLAMA_CPP_METAL=""
BUILD_TESTING=""
NUM_JOBS=""
BUILD_NCNN=false
BUILD_CURL=""
CONFIGURE_ONLY=false
BUILD_GIGA_EDGE_ONLY=false
DEPS_ONLY=false
CLEAN_ALL=false
CLEAN_BUILD=false
CLEAN_GIGA_EDGE=false
MODULES=""
PRESET=""
GIGA_EDGE_ARGS=""
DRY_RUN=false
# Taken from the environment when not given.
NDK_ROOT="${NDK_ROOT:-${ANDROID_NDK_HOME:-}}"

add_module() {
    MODULES="${MODULES:+${MODULES};}${1}"
}

has_module() {
  local module="$1"
  [[ -z "$module" ]] && return 1 # empty argument: not found
  [[ -z "${MODULES-}" ]] && return 1 # empty MODULES: not found
  [[ "$MODULES" == "*" ]] && return 0 # *: always found
  local IFS=$' ;'
  for m in $MODULES; do
    [[ "$m" == "$module" ]] && return 0
  done

  return 1
}

detect_num_jobs() {
    getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2
}


print_usage() {
    echo "Usage: $(basename "$0") --target-os <os> [--preset <preset>] [--build-tflite] [--build-onnxruntime] [--build-llama-cpp] [--build-ncnn] [OPTIONS]"
    echo ""
    echo "Required:"
    echo "  --target-os <os>             Target OS: linux_x86_64, macos_arm64, macos_x64, android_arm64, ios_arm64, linux_aarch64, windows_x64 (Git Bash + MSVC + Ninja)"
    echo ""
    echo "Backends (a preset sets them; otherwise give at least one):"
    echo "  --build-tflite               TensorFlow Lite."
    echo "  --tflite-xnnpack             Build tflite with XNNPACK (only with --build-tflite). A model enables it with"
    echo "                                its option interpreter.tflite.use_xnnpack; not applied by default."
    echo "  --build-onnxruntime          ONNX Runtime."
    echo "  --build-llama-cpp            llama.cpp."
    echo "  --llama-cpp-preset <preset>  Tuned llama.cpp build: cortex-a55, cuda, portable."
    echo "  --llama-cpp-metal <on|off>   Metal for llama.cpp on macOS and iOS. Default: on for macos_arm64, off elsewhere."
    echo "  --build-ncnn                 ncnn."
    echo ""
    echo "Transport dependencies:"
    echo "  --build-curl                 Build curl and openssl from source (for web_transport_curl)."
    echo "                                On by default when the module web_transport_curl is built."
    echo "  --no-build-curl              Do not build curl: web_transport_curl uses the system curl."
    echo ""
    echo "Modules:"
    echo "  --preset                    Preset to build: opensource, offline_assistant or a path to a preset file"
    echo "  --module                    Modules to build"
    echo ""
    echo "Build options:"
    echo "  --build-giga-edge-only       Rebuild only giga_edge: skips cleaning, downloading and building"
    echo "                               the dependencies, which must already be built."
    echo "  --deps-only                  Build only the dependencies, without giga_edge."
    echo "  --configure-only             Only configure giga_edge (compile_commands.json), without building it."
    echo "  --build-tests                Build the test targets of giga_edge."
    echo "  --skip-tests                 Do not build the test targets of giga_edge."
    echo ""
    echo "Cleaning (ignored with --build-giga-edge-only):"
    echo "  --clean-all                  Remove the whole 'build_<os>' directory."
    echo "  --clean-build                Remove everything in 'build_<os>' except 'downloads'."
    echo "  --clean-giga-edge            Remove only the build directory of giga_edge."
    echo ""
    echo "Other options:"
    echo "  --build-type <type>          Build type (Debug, Release). Default: Release."
    echo "  --jobs <n>                   Number of parallel build jobs."
    echo "  --build-dir <path>           Build directory instead of build_<os>."
    echo "  --ndk-root <path>            Android NDK (required for android_arm64)."
    echo "  --android-api <level>        Minimum Android API. Default: 31."
    echo "  --giga-edge-args <args>      Extra CMake arguments for giga_edge."
    echo "  --dry-run                    Print the computed build configuration and exit."
    echo "  -h, --help                   Show this help."
}

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --target-os) TARGET_OS="$2"; shift ;;
        --build-type) BUILD_TYPE="$2"; shift ;;
        --build-tflite) BUILD_TFLITE=true ;;
        --tflite-xnnpack) TFLITE_XNNPACK=true ;;
        --build-onnxruntime) BUILD_ONNXRUNTIME=true ;;
        --build-llama-cpp) BUILD_LLAMA_CPP=true ;;
        --llama-cpp-preset) LLAMA_CPP_PRESET="$2"; shift ;;
        --llama-cpp-metal) LLAMA_CPP_METAL="$2"; shift ;;
        --build-tests) BUILD_TESTING=true ;;
        --skip-tests) BUILD_TESTING=false ;;
        --jobs) NUM_JOBS="$2"; shift ;;
        --build-dir) BUILD_DIRECTORY_OVERRIDE="$2"; shift ;;
        --build-ncnn) BUILD_NCNN=true ;;
        --build-curl) BUILD_CURL=true ;;
        --no-build-curl) BUILD_CURL=false ;;
        --configure-only) CONFIGURE_ONLY=true ;;
        --build-giga-edge-only) BUILD_GIGA_EDGE_ONLY=true ;;
        --deps-only) DEPS_ONLY=true ;;
        --dry-run) DRY_RUN=true ;;
        --clean-all) CLEAN_ALL=true ;;
        --clean-build) CLEAN_BUILD=true ;;
        --clean-giga-edge) CLEAN_GIGA_EDGE=true ;;
        --module) add_module "$2"; shift ;;
        --preset) PRESET="$2"; shift ;;
        --giga-edge-args) GIGA_EDGE_ARGS="$2"; shift ;;
        --ndk-root) NDK_ROOT="$2"; shift ;;
        --android-api) ANDROID_API="$2"; shift ;;
        -h|--help) print_usage; exit 0 ;;
        *) echo "Unknown option: $1"; print_usage; exit 1 ;;
    esac
    shift
done

echo "Modules: ${MODULES}"

if [ -z "$TARGET_OS" ]; then
    echo "Error: --target-os is required." >&2; print_usage; exit 1;
fi
if [[ "$TARGET_OS" != "linux_x86_64" && "$TARGET_OS" != "macos_arm64" && "$TARGET_OS" != "macos_x64" && "$TARGET_OS" != "android_arm64" && "$TARGET_OS" != "ios_arm64" && "$TARGET_OS" != "linux_aarch64" && "$TARGET_OS" != "windows_x64" ]]; then
    echo "Error: unsupported target '$TARGET_OS'. Supported: linux_x86_64, macos_arm64, macos_x64, android_arm64, ios_arm64, linux_aarch64, windows_x64." >&2; print_usage; exit 1;
fi
if [[ -n "$LLAMA_CPP_PRESET" && "$LLAMA_CPP_PRESET" != "cortex-a55" && "$LLAMA_CPP_PRESET" != "cuda" && "$LLAMA_CPP_PRESET" != "portable" ]]; then
    echo "Error: unknown llama.cpp preset '$LLAMA_CPP_PRESET'. Supported: cortex-a55, cuda, portable." >&2; exit 1;
fi
if [[ -z "$LLAMA_CPP_METAL" ]]; then
    if [[ "$TARGET_OS" == "macos_arm64" ]]; then LLAMA_CPP_METAL=on; else LLAMA_CPP_METAL=off; fi
fi
if [[ "$LLAMA_CPP_METAL" != "on" && "$LLAMA_CPP_METAL" != "off" ]]; then
    echo "Error: --llama-cpp-metal takes on or off." >&2; exit 1;
fi
if [[ -n "$NUM_JOBS" && ! "$NUM_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: --jobs must be a positive integer." >&2; exit 1;
fi
if [[ -z "$BUILD_TESTING" ]]; then
    if [[ "$TARGET_OS" == "linux_aarch64" || "$TARGET_OS" == "android_arm64" || "$TARGET_OS" == "ios_arm64" ]]; then
        BUILD_TESTING=false
    else
        BUILD_TESTING=true
    fi
fi

echo "Build tests: ${BUILD_TESTING}"

if [[ -z "$NUM_JOBS" ]]; then
    NUM_JOBS="$(detect_num_jobs)"
fi

echo "Build jobs: ${NUM_JOBS}"

if [[ "$TARGET_OS" == "android_arm64" ]]; then
    if [ -z "$NDK_ROOT" ] && [ -n "${ANDROID_NDK_ROOT:-}" ]; then
        NDK_ROOT="$ANDROID_NDK_ROOT"
    fi

    if [ -z "$NDK_ROOT" ]; then
        echo "Error: building for android_arm64 needs the Android NDK." >&2
        echo "Pass --ndk-root <path> or set ANDROID_NDK_HOME." >&2
        exit 1
    fi
    if [ ! -d "$NDK_ROOT" ]; then
        echo "Error: the NDK path does not exist: $NDK_ROOT" >&2
        exit 1
    fi
    echo "Android NDK: $NDK_ROOT"
fi

# iOS 16: iPhone 8 and newer; devices only, no simulator.
IOS_CMAKE_ARGS=(
    "-DCMAKE_SYSTEM_NAME=iOS" "-DCMAKE_OSX_SYSROOT=iphoneos" "-DCMAKE_OSX_ARCHITECTURES=arm64"
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=16.0"
)

GIGA_EDGE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ "$TARGET_OS" == "windows_x64" ]]; then
    GIGA_EDGE_ROOT="$(cygpath -m "$GIGA_EDGE_ROOT")"
    [[ -n "$BUILD_DIRECTORY_OVERRIDE" ]] && BUILD_DIRECTORY_OVERRIDE="$(cygpath -m "$BUILD_DIRECTORY_OVERRIDE")"
fi
BUILD_DIRECTORY="${BUILD_DIRECTORY_OVERRIDE:-$GIGA_EDGE_ROOT/build_${TARGET_OS}}"
DOWNLOAD_DIRECTORY="$BUILD_DIRECTORY/downloads"
INSTALL_DIRECTORY="$BUILD_DIRECTORY/install"
GIGA_EDGE_BUILD_DIRECTORY="$BUILD_DIRECTORY/giga_edge"

echo "Project root: $GIGA_EDGE_ROOT"
echo "Build directory: $BUILD_DIRECTORY"

# Backends and modules of the preset (tools/scripts/preset_ci_info.cmake).
PRESET_MODULES=""
if [ -n "$PRESET" ]; then
    PRESET_INFO="$(cmake -DPRESET="$PRESET" -DGIGA_EDGE_ROOT="$GIGA_EDGE_ROOT" -P "$GIGA_EDGE_ROOT/tools/scripts/preset_ci_info.cmake")"
    preset_get() { sed -n "s/^$1=//p" <<< "$PRESET_INFO"; }
    PRESET_MODULES="$(preset_get MODULES)"
    PRESET_BUILD_FLAGS=" $(preset_get BUILD_FLAGS) "
    [[ "$PRESET_BUILD_FLAGS" == *" --build-tflite "* ]] && BUILD_TFLITE=true
    [[ "$PRESET_BUILD_FLAGS" == *" --tflite-xnnpack "* ]] && TFLITE_XNNPACK=true
    [[ "$PRESET_BUILD_FLAGS" == *" --build-onnxruntime "* ]] && BUILD_ONNXRUNTIME=true
    [[ "$PRESET_BUILD_FLAGS" == *" --build-llama-cpp "* ]] && BUILD_LLAMA_CPP=true
    [[ "$PRESET_BUILD_FLAGS" == *" --build-ncnn "* ]] && BUILD_NCNN=true
    echo "Preset '$PRESET': modules [$PRESET_MODULES], derived flags:$PRESET_BUILD_FLAGS"
fi

# SDK dependencies for the modules and build options (tools/scripts/sdk_deps_info.cmake).
SDK_DEPS_INFO_ARGS=(
    "-DPRESET=$PRESET" "-DMODULES=$MODULES" "-DTARGET_OS=$TARGET_OS"
    "-DGIGA_EDGE_ARGS=$GIGA_EDGE_ARGS" "-DGIGA_EDGE_ROOT=$GIGA_EDGE_ROOT"
    "-DBUILD_TESTING=$BUILD_TESTING"
    "-DBUILD_TFLITE=$BUILD_TFLITE" "-DBUILD_ONNXRUNTIME=$BUILD_ONNXRUNTIME"
)
SDK_DEPS="$(cmake "${SDK_DEPS_INFO_ARGS[@]}" -P "$GIGA_EDGE_ROOT/tools/scripts/sdk_deps_info.cmake" | sed -n 's/^SDK_DEPS=//p')"
echo "SDK deps: $SDK_DEPS"

if [ "$TFLITE_XNNPACK" = true ] && [ "$BUILD_TFLITE" != true ]; then
    echo "Error: --tflite-xnnpack only makes sense together with --build-tflite." >&2
    exit 1
fi

if has_module "enhance" || [[ " $PRESET_MODULES " == *" enhance "* ]]; then
  BUILD_ENHANCE=true
else
  BUILD_ENHANCE=false
fi

# Three states: an explicit --build-curl/--no-build-curl wins over the default; empty means derived from the modules.
if [ -z "$BUILD_CURL" ]; then
  BUILD_CURL=false
  if has_module "web_transport_curl" || [[ " $PRESET_MODULES " == *" web_transport_curl "* ]]; then
    BUILD_CURL=true
  fi
fi

# Shared libraries (protobuf, flatbuffers, abseil, cpuinfo, re2) are needed by the heavy backends and by the modules
# with protobuf schemas or serialization: model, tts.
NEEDS_COMMON_DEPS=false
if [ "$BUILD_TFLITE" = true ] || [ "$BUILD_ONNXRUNTIME" = true ] || [ "$BUILD_NCNN" = true ] \
   || [ "$BUILD_ENHANCE" = true ] || [ "$MODULES" == '*' ]; then
    NEEDS_COMMON_DEPS=true
fi
for _common_dep_module in model tts; do
    if has_module "$_common_dep_module" || [[ " $PRESET_MODULES " == *" $_common_dep_module "* ]]; then
        NEEDS_COMMON_DEPS=true
    fi
done

if [ "$DRY_RUN" = true ]; then
    echo "DRY-RUN target_os=$TARGET_OS build_type=$BUILD_TYPE"
    echo "DRY-RUN tflite=$BUILD_TFLITE tflite_xnnpack=$TFLITE_XNNPACK onnxruntime=$BUILD_ONNXRUNTIME llama_cpp=$BUILD_LLAMA_CPP ncnn=$BUILD_NCNN enhance=$BUILD_ENHANCE curl=$BUILD_CURL common_deps=$NEEDS_COMMON_DEPS"
    echo "DRY-RUN preset=$PRESET modules=$MODULES preset_modules=$PRESET_MODULES"
    echo "DRY-RUN build_testing=$BUILD_TESTING giga_edge_args=$GIGA_EDGE_ARGS"
    exit 0
fi

if [ "$BUILD_GIGA_EDGE_ONLY" = true ]; then
    echo "--- --build-giga-edge-only: skipping cleaning, downloading and building the dependencies. ---"
    echo "--- The dependencies must already be built in $INSTALL_DIRECTORY ---"
    if [ ! -d "$INSTALL_DIRECTORY" ] || [ -z "$(ls -A "$INSTALL_DIRECTORY")" ]; then
        echo "Error: the built dependencies ($INSTALL_DIRECTORY) are missing or empty." >&2
        echo "Run the build without --build-giga-edge-only at least once." >&2
        exit 1
    fi
else
    if [ "$CLEAN_ALL" = true ] && [ -d "$BUILD_DIRECTORY" ]; then
        echo "--- --clean-all: removing the whole $BUILD_DIRECTORY ---"
        read -p "Remove it? [y/N]" -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$BUILD_DIRECTORY"
        fi
    elif [ "$CLEAN_BUILD" = true ] && [ -d "$BUILD_DIRECTORY" ]; then
        echo "--- --clean-build: removing everything except $DOWNLOAD_DIRECTORY ---"
        read -p "Remove it? [y/N]" -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            find "$BUILD_DIRECTORY" -mindepth 1 -maxdepth 1 -not -name "$(basename "$DOWNLOAD_DIRECTORY")" -exec rm -rf {} +
        fi
    elif [ "$CLEAN_GIGA_EDGE" = true ] && [ -d "$GIGA_EDGE_BUILD_DIRECTORY" ]; then
        echo "--- --clean-giga-edge: removing $GIGA_EDGE_BUILD_DIRECTORY ---"
        rm -rf "$GIGA_EDGE_BUILD_DIRECTORY"
    fi

    echo "--- Preparing the build directories ---"
    mkdir -p "$DOWNLOAD_DIRECTORY"
    mkdir -p "$INSTALL_DIRECTORY"

    downloader_args=()
    if [ "$BUILD_TFLITE" = true ]; then
        downloader_args+=("--tflite")
    fi
    if [ "$BUILD_ONNXRUNTIME" = true ]; then
        downloader_args+=("--onnx")
    fi
    if [ "$BUILD_LLAMA_CPP" = true ]; then
        downloader_args+=("--llama")
    fi
    if [ "$BUILD_ENHANCE" = true ]; then
        downloader_args+=("--enhance")
    fi
    if [ "$BUILD_NCNN" = true ]; then
        downloader_args+=("--ncnn")
    fi
    if [ "$BUILD_CURL" = true ]; then
        downloader_args+=("--curl")
    fi
    if [ "$NEEDS_COMMON_DEPS" = false ]; then
        downloader_args+=("--no-common")
    fi
    if [[ ( "$TARGET_OS" == "android_arm64" || "$TARGET_OS" == "linux_aarch64" || "$TARGET_OS" == "ios_arm64" ) && "$BUILD_LLAMA_CPP" = true ]]; then
        downloader_args+=("--kleidiai")
    fi
    if [[ "$TARGET_OS" == "windows_x64" && "$BUILD_ONNXRUNTIME" = true ]]; then
        downloader_args+=("--windows")
    fi
    # The second call (it runs on a non-empty downloads too) gets XNNPACK: the flag can be turned on for an existing
    # build.
    sdk_downloader_extra=()
    if [ "$TFLITE_XNNPACK" = true ]; then
        sdk_downloader_extra+=(--xnnpack)
    fi
    # Always: each entry checks its directory first, so a repeated run only finishes an interrupted download.
    export DOWNLOAD_DIRECTORY
    "$GIGA_EDGE_ROOT/tools/scripts/download_dependencies.sh" "${downloader_args[@]}"
    # SDK dependencies (and XNNPACK) follow the modules and flags of the build.
    DOWNLOAD_DIRECTORY="$DOWNLOAD_DIRECTORY" "$GIGA_EDGE_ROOT/tools/scripts/download_dependencies.sh" --no-common \
        --sdk-deps "$SDK_DEPS" ${sdk_downloader_extra[@]+"${sdk_downloader_extra[@]}"}

    DEPS_BUILD_DIR="$BUILD_DIRECTORY/dependencies"
    mkdir -p "$DEPS_BUILD_DIR"
    cd "$DEPS_BUILD_DIR"

    CMAKE_DEPS_ARGS=(
      "-DCMAKE_BUILD_TYPE=Release"
      "-DCMAKE_INSTALL_PREFIX=${INSTALL_DIRECTORY}"
      "-DDEPS_SOURCE_DIR=${DOWNLOAD_DIRECTORY}"
      "-DTARGET_OS=${TARGET_OS}"
      "-DBUILD_TFLITE:BOOL=${BUILD_TFLITE}"
      "-DTFLITE_ENABLE_XNNPACK:BOOL=${TFLITE_XNNPACK}"
      "-DBUILD_ONNXRUNTIME:BOOL=${BUILD_ONNXRUNTIME}"
      "-DBUILD_LLAMA_CPP:BOOL=${BUILD_LLAMA_CPP}"
      "-DBUILD_NCNN:BOOL=${BUILD_NCNN}"
      "-DBUILD_CURL:BOOL=${BUILD_CURL}"
      "-DBUILD_COMMON_DEPS:BOOL=${NEEDS_COMMON_DEPS}"
      "-DSDK_DEPS=${SDK_DEPS// /;}"
    )

    if [ -n "$LLAMA_CPP_PRESET" ]; then
        CMAKE_DEPS_ARGS+=("-DLLAMA_CPP_PRESET=${LLAMA_CPP_PRESET}")
    fi
    if [ "$LLAMA_CPP_METAL" == "on" ]; then
        CMAKE_DEPS_ARGS+=("-DLLAMA_CPP_METAL=ON")
    else
        CMAKE_DEPS_ARGS+=("-DLLAMA_CPP_METAL=OFF")
    fi

    if [ "$TARGET_OS" == "linux_x86_64" ]; then
        CMAKE_DEPS_ARGS+=("-DCMAKE_CXX_FLAGS=-fPIC ${CXXFLAGS:-}" "-DCMAKE_C_FLAGS=-fPIC ${CFLAGS:-}")
    elif [ "$TARGET_OS" == "windows_x64" ]; then
        CMAKE_DEPS_ARGS+=("-G" "Ninja" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" "-DCMAKE_SYSTEM_NAME=Windows" "-DCMAKE_SYSTEM_PROCESSOR=AMD64")
    elif [ "$TARGET_OS" == "macos_arm64" ]; then
        CMAKE_DEPS_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=arm64")
    elif [ "$TARGET_OS" == "macos_x64" ]; then
        CMAKE_DEPS_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=x86_64" "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0")
    elif [ "$TARGET_OS" == "ios_arm64" ]; then
        CMAKE_DEPS_ARGS+=("${IOS_CMAKE_ARGS[@]}")
    elif [ "$TARGET_OS" == "linux_aarch64" ]; then
        echo "--- Linux Aarch64 ---"
        CMAKE_DEPS_ARGS+=(
            "-DCMAKE_TOOLCHAIN_FILE=$GIGA_EDGE_ROOT/tools/cmake/linux_aarch64.toolchain.cmake"
            "-DCMAKE_CXX_FLAGS=-fPIC ${CXXFLAGS:-}"
            "-DCMAKE_C_FLAGS=-fPIC ${CFLAGS:-}"
        )
    elif [ "$TARGET_OS" == "android_arm64" ]; then
        CMAKE_DEPS_ARGS+=(
            "-DCMAKE_TOOLCHAIN_FILE=$NDK_ROOT/build/cmake/android.toolchain.cmake"
            "-DANDROID_ABI=arm64-v8a"
            "-DANDROID_PLATFORM=android-${ANDROID_API:-31}"
            "-DANDROID_STL=c++_shared"
            "-DANDROID_NDK=$NDK_ROOT"
            # The NDK toolchain resets CMAKE_C(XX)_FLAGS in the cache: CFLAGS/CXXFLAGS of the environment are passed
            # explicitly.
            "-DCMAKE_C_FLAGS=${CFLAGS:-}"
            "-DCMAKE_CXX_FLAGS=${CXXFLAGS:-}"
        )
    fi

    echo "--- Configuring the dependencies ---"
    cmake "${CMAKE_DEPS_ARGS[@]}" "$GIGA_EDGE_ROOT/tools/dependencies/"

    echo "--- Building the dependencies with ${NUM_JOBS} jobs ---"
    cmake --build . --config ${BUILD_TYPE} -- -j${NUM_JOBS}
    echo "--- The dependencies are built. ---"
    if [ "$DEPS_ONLY" = true ]; then
        echo "--- --deps-only: the dependencies are ready, giga_edge is not built. ---"
        exit 0
    fi
fi

# giga_edge is always built.
echo ""
echo "--- Building giga_edge ---"
mkdir -p "$GIGA_EDGE_BUILD_DIRECTORY"
cd "$GIGA_EDGE_BUILD_DIRECTORY"

GIGA_EDGE_CMAKE_ARGS=(
    "-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${INSTALL_DIRECTORY}"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=1"
    "-DBUILD_TESTING:BOOL=${BUILD_TESTING}"
    "-DTFLITE_ENABLED:BOOL=${BUILD_TFLITE}"
    "-DTFLITE_XNNPACK:BOOL=${TFLITE_XNNPACK}"
    "-DONNXRUNTIME_ENABLED:BOOL=${BUILD_ONNXRUNTIME}"
    "-DLLAMA_CPP_ENABLED:BOOL=${BUILD_LLAMA_CPP}"
    "-DNCNN_ENABLED:BOOL=${BUILD_NCNN}"
    "-DPKG_CONFIG_PATH=${INSTALL_DIRECTORY}/lib/pkgconfig/"
    "-DDEPS_SOURCE_DIR=${DOWNLOAD_DIRECTORY}"
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
    ${GIGA_EDGE_ARGS}
)

# An empty MODULES would override the modules of -DPRESET.
if [[ -n "${MODULES}" ]]; then
    GIGA_EDGE_CMAKE_ARGS+=("-DMODULES=${MODULES}")
fi
if [[ -n "${PRESET}" ]]; then
    GIGA_EDGE_CMAKE_ARGS+=("-DPRESET=${PRESET}")
fi

HOST_PROTOC_EXE="${BUILD_DIRECTORY}/dependencies/host_tools_install/bin/protoc"
[[ "$TARGET_OS" == "windows_x64" ]] && HOST_PROTOC_EXE="${HOST_PROTOC_EXE}.exe"
if [ -x "${HOST_PROTOC_EXE}" ]; then
    GIGA_EDGE_CMAKE_ARGS+=(
        "-DProtobuf_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"
        "-Dprotobuf_PROTOC_EXECUTABLE=${HOST_PROTOC_EXE}"
    )
fi

if [ "$TARGET_OS" == "macos_arm64" ]; then
    GIGA_EDGE_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}")

elif [ "$TARGET_OS" == "macos_x64" ]; then
    GIGA_EDGE_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}" "-DCMAKE_OSX_ARCHITECTURES=x86_64" "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0")

elif [ "$TARGET_OS" == "ios_arm64" ]; then
    GIGA_EDGE_CMAKE_ARGS+=(
        "${IOS_CMAKE_ARGS[@]}"
        "-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}"
        "-DCMAKE_FIND_ROOT_PATH=${INSTALL_DIRECTORY}"
        "-DCMAKE_IGNORE_PATH=/opt/homebrew;/usr/local"
    )

elif [ "$TARGET_OS" == "linux_x86_64" ]; then
    GIGA_EDGE_CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}")

elif [ "$TARGET_OS" == "windows_x64" ]; then
    GIGA_EDGE_CMAKE_ARGS+=("-G" "Ninja" "-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}" "-DCMAKE_SYSTEM_NAME=Windows" "-DCMAKE_SYSTEM_PROCESSOR=AMD64")

elif [ "$TARGET_OS" == "linux_aarch64" ]; then
    echo "--- Linux Aarch64 ---"
    GIGA_EDGE_CMAKE_ARGS+=(
        "-DCMAKE_TOOLCHAIN_FILE=$GIGA_EDGE_ROOT/tools/cmake/linux_aarch64.toolchain.cmake"
        "-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}"
        "-DCMAKE_FIND_ROOT_PATH=${INSTALL_DIRECTORY}"
    )

elif [ "$TARGET_OS" == "android_arm64" ]; then
    echo "--- Configuring giga_edge for Android ---"

    GIGA_EDGE_CMAKE_ARGS+=(
        "-DCMAKE_SYSTEM_NAME=Android"
        "-DCMAKE_TOOLCHAIN_FILE=$NDK_ROOT/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=arm64-v8a"
        "-DANDROID_PLATFORM=android-${ANDROID_API:-31}"
        "-DANDROID_STL=c++_shared"
        "-DANDROID_NDK=$NDK_ROOT"

        "-DCMAKE_C_FLAGS=${CFLAGS:-}"
        "-DCMAKE_CXX_FLAGS=${CXXFLAGS:-} -Wno-error=deprecated-declarations -Wno-deprecated-declarations"

        "-DOPENSSL_SUPPRESS_DEPRECATED=1"

        "-DCMAKE_FIND_ROOT_PATH=${INSTALL_DIRECTORY};${NDK_ROOT}"

        "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY"
        "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY"
        "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY"

        "-DCMAKE_IGNORE_PATH=/opt/homebrew;/usr/local"

        "-DCMAKE_PREFIX_PATH=${INSTALL_DIRECTORY}"
    )
fi

echo "--- Configuring giga_edge ---"
cmake "${GIGA_EDGE_CMAKE_ARGS[@]}" "$GIGA_EDGE_ROOT"

if [ "$CONFIGURE_ONLY" = true ]; then
    echo "--- --configure-only: giga_edge is not built ---"
    exit 0
fi

echo "--- Building giga_edge with ${NUM_JOBS} jobs ---"
cmake --build . --config ${BUILD_TYPE} -- -j${NUM_JOBS}

echo ""
echo "--- Build finished ---"
echo "Dependencies: $INSTALL_DIRECTORY"
echo "giga_edge: $GIGA_EDGE_BUILD_DIRECTORY"
