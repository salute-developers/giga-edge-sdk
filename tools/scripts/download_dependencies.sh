#!/bin/bash
# Downloads the sources of the third-party dependencies into $DOWNLOAD_DIRECTORY: upstream repositories at pinned
# commits (or release archives with a pinned sha256) plus our changes from tools/dependencies/patches-oss/<path>/.
# Called by tools/scripts/build_giga_edge.sh.
set -eo pipefail

# Patches must apply with the line endings of the repository, also under Git for Windows (autocrlf=true by default).
# Added to the GIT_CONFIG_* of the environment, if any.
git_config_n="${GIT_CONFIG_COUNT:-0}"
export "GIT_CONFIG_KEY_$git_config_n=core.autocrlf" "GIT_CONFIG_VALUE_$git_config_n=false"
export GIT_CONFIG_COUNT=$((git_config_n + 1))
# The sources are cloned without their Git LFS files; an entry names the ones the build needs (--lfs).
export GIT_LFS_SKIP_SMUDGE=1

DOWNLOAD_TFLITE=false
DOWNLOAD_ONNXRUNTIME=false
DOWNLOAD_LLAMA_CPP=false
DOWNLOAD_KLEIDIAI=false
DOWNLOAD_XNNPACK=false
DOWNLOAD_ENHANCE=false
DOWNLOAD_NCNN=false
DOWNLOAD_CURL=false
DOWNLOAD_WINDOWS=false
DOWNLOAD_COMMON=true
SDK_DEPS=""

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --tflite) DOWNLOAD_TFLITE=true ;;
        --onnx) DOWNLOAD_ONNXRUNTIME=true ;;
        --llama) DOWNLOAD_LLAMA_CPP=true ;;
        --enhance) DOWNLOAD_ENHANCE=true ;;
        --ncnn) DOWNLOAD_NCNN=true; ;;
        --curl) DOWNLOAD_CURL=true ;;
        --windows) DOWNLOAD_WINDOWS=true ;;
        --kleidiai) DOWNLOAD_KLEIDIAI=true ;;
        --xnnpack) DOWNLOAD_XNNPACK=true ;;
        --no-common) DOWNLOAD_COMMON=false ;;
        --sdk-deps) SDK_DEPS="$2"; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

if [ -z "${DOWNLOAD_DIRECTORY:-}" ]; then
    echo "DOWNLOAD_DIRECTORY is not set" >&2
    exit 1
fi

# Network steps are retried on transient failures (see net_retry.sh). Absolute path: the script changes directories.
NET_RETRY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/net_retry.sh"
git_clone() {
    "$NET_RETRY" git clone "$@"
}
# git_clone_rev <url> <commit> <directory>: shallow clone of exactly this commit (not of a branch)
# (on failure the directory is removed, like git clone does). A clone that is already there is not downloaded again,
# one at another commit is switched to the needed one: a repeated run on a non-empty downloads is safe (XNNPACK is
# downloaded there too, see --xnnpack).
git_clone_rev() {
    if [ -d "$3/.git" ]; then
        if [ "$(git -C "$3" rev-parse HEAD)" != "$2" ]; then
            # Our patches (applied at the end of the script) in the clone would break checkout: such a clone is not
            # switched, it has to be downloaded again.
            if ! git -C "$3" diff --quiet --no-ext-diff HEAD || ! git -C "$3" diff --quiet --no-ext-diff --cached HEAD; then
                echo "$3: the clone is at $(git -C "$3" rev-parse --short HEAD), $2 is needed, and it has changes (our patches?):" >&2
                echo "remove $3 and run again" >&2
                return 1
            fi
            "$NET_RETRY" git -C "$3" fetch -q --depth 1 "$1" "$2"
            git -C "$3" checkout -q FETCH_HEAD
        fi
        return 0
    fi
    # A foreign non-empty directory is left alone: on a failure below it would be removed.
    if [ -d "$3" ] && [ -n "$(ls -A "$3")" ]; then
        echo "$3: the directory is not empty and not a git clone — remove it and run again" >&2
        return 1
    fi
    git init -q "$3"
    if ! { "$NET_RETRY" git -C "$3" fetch -q --depth 1 "$1" "$2" && git -C "$3" checkout -q FETCH_HEAD; }; then
        rm -rf "$3"
        return 1
    fi
    git -C "$3" remote add origin "$1"
}

# Clone pinned to a commit, with submodules. A repeated run fetches what is missing and switches an existing clone to
# the commit, so it is safe on a non-empty DOWNLOAD_DIRECTORY.
clone_pinned() {
    local dir="$DOWNLOAD_DIRECTORY/$1" url="$2" rev="$3"
    if [ ! -d "$dir/.git" ] && [ -d "$dir" ] && [ -n "$(ls -A "$dir")" ]; then
        echo "$dir: the directory is not empty and not a git clone — remove it and run again" >&2
        return 1
    fi
    if [ ! -d "$dir/.git" ]; then
        git_clone "$url" "$dir"
    fi
    if [ "$(git -C "$dir" rev-parse HEAD)" != "$rev" ]; then
        git -C "$dir" cat-file -e "$rev^{commit}" 2>/dev/null || "$NET_RETRY" git -C "$dir" fetch origin
        # The commit is not in origin either: this is a clone of another repository under the same name, so origin is
        # switched.
        if ! git -C "$dir" cat-file -e "$rev^{commit}" 2>/dev/null; then
            echo "--- $1: commit $rev is not in $(git -C "$dir" remote get-url origin), taking it from $url ---"
            git -C "$dir" remote set-url origin "$url"
            # The clone could be single-branch: take all branches of the new repository.
            git -C "$dir" config remote.origin.fetch '+refs/heads/*:refs/remotes/origin/*'
            "$NET_RETRY" git -C "$dir" fetch origin
        fi
        git -C "$dir" checkout -q "$rev"
    fi
    if [ -f "$dir/.gitmodules" ]; then
        "$NET_RETRY" git -C "$dir" submodule update --init --recursive
    fi
}

GIGA_EDGE_ROOT="${GIGA_EDGE_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
OSS_PATCHES="$GIGA_EDGE_ROOT/tools/dependencies/patches-oss"
IDENTITY=(-c "user.name=Giga Edge SDK" -c "user.email=noreply@giga-edge-sdk.invalid")

sha256_of() {
    if command -v sha256sum > /dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -c1-64
}

# fetch_archive <url> <sha256> <directory>: release archive with a checked sha256, unpacked without its top-level
# directory into a git repository (one commit, all modes 100644: the same tree on every OS).
fetch_archive() {
    local url="$1" sum="$2" dir="$3" file="$3.archive"
    "$NET_RETRY" curl -fsSL -o "$file" "$url"
    if [ "$(sha256_of "$file")" != "$sum" ]; then
        rm -f "$file"
        echo "$url: sha256 does not match $sum" >&2
        return 1
    fi
    mkdir "$dir"
    # From the directory of the archive by a relative name: GNU tar of Git Bash takes "C:/..." for a remote host.
    (cd "$(dirname "$dir")" && tar -xf "$(basename "$file")" -C "$(basename "$dir")" --strip-components=1)
    rm -f "$file"
    git -C "$dir" init -q
    # Modes in the tree are 100644 on every OS; executable bits stay in the working tree (OpenSSL Configure).
    git -C "$dir" config core.fileMode false
    git -C "$dir" add -A -f
    git "${IDENTITY[@]}" -C "$dir" commit -q -m "$url"
}

# fetched_as_expected <directory> <ref> <tree>: the directory is this commit (no tree given) or this tree.
fetched_as_expected() {
    [ -d "$1/.git" ] || return 1
    if [ -n "$3" ]; then
        [ "$(git -C "$1" rev-parse 'HEAD^{tree}' 2>/dev/null)" = "$3" ]
    else
        [ "$(git -C "$1" rev-parse HEAD 2>/dev/null)" = "$2" ]
    fi
}

# fetch <path> <kind> <url> <ref|sha256> [--tag <name>] [--mkdir <path>] [--oss-tree <tree>] [--lfs <file>]
# The only place that puts something into downloads; <path> is relative to downloads. Kinds: pinned is a commit with
# submodules (clone_pinned); rev is a commit without submodules (git_clone_rev); archive is a release archive and its
# sha256. --oss-tree is the tree after our patches from tools/dependencies/patches-oss/<path> (required for archive);
# --lfs is a file that the repository keeps in Git LFS and the build needs.
# An existing directory is checked first and left as it is when it matches; a new one is prepared in <path>.tmp
# and moved into place only when complete, so an interrupted run leaves no half-patched sources.
fetch() {
    local path="$1" kind="$2" url="$3" ref="$4" tag="" mkd="" tree="" lfs=""
    shift 4
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --tag) tag="$2"; shift ;;
            --mkdir) mkd="$2"; shift ;;
            --oss-tree) tree="$2"; shift ;;
            --lfs) lfs="$2"; shift ;;
            *) echo "fetch $path: unknown option $1" >&2; exit 1 ;;
        esac
        shift
    done
    local dir="$DOWNLOAD_DIRECTORY/$path"
    if [ -e "$dir" ]; then
        if fetched_as_expected "$dir" "$ref" "$tree"; then
            return 0
        fi
        echo "$dir does not match this version of the sources: delete it and run with --clean-build" >&2
        exit 1
    fi
    rm -rf "$dir.tmp"
    mkdir -p "$(dirname "$dir")"
    case "$kind" in
        pinned) clone_pinned "$path.tmp" "$url" "$ref" ;;
        rev) git_clone_rev "$url" "$ref" "$dir.tmp" ;;
        archive) fetch_archive "$url" "$ref" "$dir.tmp" ;;
        *) echo "fetch $path: unknown kind $kind" >&2; exit 1 ;;
    esac
    if [ -d "$OSS_PATCHES/$path" ]; then
        # A patched file gets the mode of the index, 100644 in an archive: keep the executable bits of the release.
        local exe=""
        if [ "$kind" = archive ]; then
            exe="$(cd "$dir.tmp" && find . -path ./.git -prune -o -type f -perm -u+x -print)"
        fi
        for p in "$OSS_PATCHES/$path"/*.patch; do
            [ -e "$p" ] || continue
            git -C "$dir.tmp" apply --index --whitespace=nowarn "$p" \
                || { echo "fetch $path: $(basename "$p") does not apply" >&2; exit 1; }
        done
        if [ -n "$exe" ]; then
            (cd "$dir.tmp" && printf '%s\n' "$exe" | while IFS= read -r f; do [ ! -f "$f" ] || chmod +x "$f"; done)
        fi
        git "${IDENTITY[@]}" -C "$dir.tmp" commit -q -m "Giga Edge SDK patches"
    fi
    if ! fetched_as_expected "$dir.tmp" "$ref" "$tree"; then
        echo "fetch $path: the result does not match ${tree:-$ref}" >&2
        exit 1
    fi
    if [ -n "$lfs" ]; then
        git -C "$dir.tmp" lfs install --local > /dev/null 2>&1 \
            || { echo "fetch $path: $lfs is kept in Git LFS (https://git-lfs.com): install it and run again" >&2; exit 1; }
        git -C "$dir.tmp" config lfs.url "$url/info/lfs"
        "$NET_RETRY" git -C "$dir.tmp" lfs pull --include "$lfs"
    fi
    if [ -n "$tag" ]; then git -C "$dir.tmp" tag -f "$tag"; fi
    if [ -n "$mkd" ]; then mkdir -p "$dir.tmp/$mkd"; fi
    mv "$dir.tmp" "$dir"
}

# Eigen of the tflite and onnxruntime backends is shared with the SDK: when the script runs directly, without
# --sdk-deps from build_giga_edge.sh, download it too.
if { [ "$DOWNLOAD_TFLITE" = true ] || [ "$DOWNLOAD_ONNXRUNTIME" = true ]; } && [[ " $SDK_DEPS " != *" eigen "* ]]; then
    SDK_DEPS="$SDK_DEPS eigen"
fi

GH=https://github.com
# onnxruntime is no longer on GitHub: the fork of Intel keeps its upstream history with our base commit. The environment
# variable GIGA_EDGE_ORT_REPO sets another address with that commit (see README).
GIGA_EDGE_ORT_REPO="${GIGA_EDGE_ORT_REPO:-https://github.com/intel/onnxruntime.git}"

if [ "$DOWNLOAD_COMMON" = true ]; then
    echo "--- Downloading common dependencies ---"
    fetch abseil-cpp rev $GH/abseil/abseil-cpp.git bc257a88f7c1939f24e0379f14a3589e926c950c
    fetch re2 rev $GH/google/re2.git 6dcd83d60f7944926bfd308cc13979fc53dd69ca
    fetch protobuf rev $GH/protocolbuffers/protobuf.git f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c \
        --oss-tree 2bc0d8e80b319b082b1ad7868079b22bc5c07bff
    # cpuinfo is at the pin of tflite 2.20 and its XNNPACK (newer than the pin of onnxruntime 1.23, compatible); our
    # change is a patch in tools/dependencies/patches/cpuinfo.
    fetch cpuinfo rev $GH/pytorch/cpuinfo.git de0ce7c7251372892e53ce9bc891750d2c9a4fd8
    fetch flatbuffers rev $GH/google/flatbuffers.git 595bf0007ab1929570c7671f091313c8fc20644e
fi

# GigaEdge Enhance, the speech enhancement library of the enhance module; it is built together with the SDK. Of its
# Git LFS files (models, test audio) the build needs only the patch to rnnoise.
if [ "$DOWNLOAD_ENHANCE" = true ]; then
    fetch enhance rev $GH/salute-developers/giga-edge-enhance.git 864b1e81ebed46c71c7a9a7363258b4258585628 \
        --lfs cmake/patches/rnnoise.patch
fi

if [ "$DOWNLOAD_TFLITE" = true ]; then
    echo "--- Downloading TensorFlow Lite dependencies ---"
    fetch farmhash rev $GH/google/farmhash.git 0d859a811870d10f53a594927d0d0b97573ad06d \
        --oss-tree 57b9ff94ce1e3a6915e1002cab3d0e47d747a100
    fetch fft2d rev $GH/petewarden/OouraFFT.git c6fd2dd6d21397baa6653139d31d84540d5449a2 \
        --oss-tree 149cacb57cbb81eb98b36a71d6059b18792fdc64
    # gemmlowp, ml_dtypes, ruy, fp16 are at the commits of tensorflow/lite/tools/cmake/modules of TF 2.20.
    fetch gemmlowp rev $GH/google/gemmlowp.git 16e8662c34917be0065110bfcd9cc27d30f52fdf
    fetch ml_dtypes rev $GH/jax-ml/ml_dtypes.git 00d98cd92ade342fef589c0470379abb27baebe9
    fetch neon2sse rev $GH/intel/ARM_NEON_2_x86_SSE.git a15b489e1222b2087007546b4912e21293ea86ff
    fetch ruy rev $GH/google/ruy.git 3286a34cc8de6149ac6844107dfdffac91531e72
    fetch tensorflow rev $GH/tensorflow/tensorflow.git 72fbba3d20f4616d7312b5e2b7f79daf6e82f2fa
    # fp16 (headers) is needed by the tflite kernels since TF 2.20, also without XNNPACK.
    fetch fp16 rev $GH/Maratyszcza/FP16.git 0a92994d729ff76a58f692d3028ca1b64b145d91
fi

# XNNPACK for tflite (build_giga_edge.sh --tflite-xnnpack): XNNPACK at the commit of
# tensorflow/lite/tools/cmake/modules/xnnpack.cmake of TF 2.20, pthreadpool and fxdiv at the commits of
# cmake/Download*.cmake of this XNNPACK (nothing else uses them: onnxruntime is built without its XNNPACK EP). The
# block does not depend on --tflite: build_giga_edge.sh passes --xnnpack to the second call too, so the flag can be
# turned on for an existing build.
if [ "$DOWNLOAD_XNNPACK" = true ]; then
    echo "--- Downloading XNNPACK for TensorFlow Lite ---"
    fetch xnnpack rev $GH/google/XNNPACK.git 585e73e63cb35c8a416c83a48ca9ab79f7f7d45e
    fetch pthreadpool rev $GH/google/pthreadpool.git c2ba5c50bb58d1397b693740cf75fad836a0d1bf
    fetch fxdiv rev $GH/Maratyszcza/FXdiv.git b408327ac2a15ec3e43352421954f5b1967701d1
fi

if [ "$DOWNLOAD_ONNXRUNTIME" = true ]; then
    echo "--- Downloading ONNX Runtime dependencies ---"
    # Boost CMake support files for mp11 (see tools/dependencies/patches-oss/mp11).
    fetch cmake rev $GH/boostorg/cmake.git 81b08178c629b94b67e3baff5b1b9bee25db7f3a
    fetch date rev $GH/HowardHinnant/date.git 6e921e1b1d21e84a5c82416ba7ecd98e33a436d0
    fetch GSL rev $GH/microsoft/GSL.git a3534567187d2edc428efd3f13466ff75fe5805c
    fetch json rev $GH/nlohmann/json.git 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
    fetch mp11 rev $GH/boostorg/mp11.git 0a0b5fb001ce0233ae3a6f99d849c0649e5a7361 \
        --oss-tree 49469b0da5dce49ce6f92c655fc7972c93b1fb09
    fetch onnx rev $GH/onnx/onnx.git e709452ef2bbc1d113faf678c24e6d3467696e83 \
        --oss-tree 23864333e584b28cf4a0e81494b30dcd60d72d18
    fetch onnxruntime rev "$GIGA_EDGE_ORT_REPO" 7a6cef6fe3672c5d59f1925e5f7c0a03dcfbc90e \
        --oss-tree 48908cc64e3b5573550f999d22f2bf5bb61c6dcc
    fetch SafeInt rev $GH/dcleblanc/SafeInt.git 4cafc9196c4da9c817992b20f5253ef967685bf8
fi

if [ "$DOWNLOAD_LLAMA_CPP" = true ]; then
    echo "--- Downloading llama.cpp dependencies ---"
    fetch llama.cpp rev $GH/ggml-org/llama.cpp.git d81235049384534c167caea52b85a694f6103d14 \
        --oss-tree 24671c8ee519082f67051a0450b828a9fc56d96d
fi
if [ "$DOWNLOAD_KLEIDIAI" = true ]; then
    # ARM kernels for llama.cpp; the commit is release v1.24.0 that ggml expects.
    fetch kleidiai pinned $GH/ARM-software/kleidiai.git 0b2ee513397578a272a4e8a6c12589ef6534371c
fi

if [ "$DOWNLOAD_NCNN" = true ]; then
    echo "--- Downloading ncnn dependencies ---"
    fetch ncnn rev $GH/Tencent/ncnn.git c4193aadbbb56582aa87b1850dd3d98fb8fd936d
fi

if [ "$DOWNLOAD_CURL" = true ]; then
    echo "--- Downloading curl dependencies ---"
    fetch curl rev $GH/curl/curl.git 01346829096c61b372692f6dc43ffa778c6caccd
fi

if [ "$DOWNLOAD_CURL" = true ]; then
    echo "--- Downloading openssl ---"
    fetch openssl-3.6.0 archive \
        $GH/openssl/openssl/releases/download/openssl-3.6.0/openssl-3.6.0.tar.gz \
        b6a5f44b7eb69e3fa35dbf15524405b44837a481d43d81daddde3ff21fcbb8e9 \
        --oss-tree 93d4d1dc0d8e875bdc80498ee7575bcac124caad
fi

if [ "$DOWNLOAD_WINDOWS" = true ]; then
    echo "--- Downloading Windows dependencies ---"
    fetch wil rev $GH/microsoft/wil.git 55f373e164d3f092dd6c7a56e3de6f90c4c6f3dc
fi

# SDK dependencies (the list comes from tools/scripts/sdk_deps_info.cmake).
if [ -n "$SDK_DEPS" ]; then
    echo "--- Downloading SDK dependencies: $SDK_DEPS ---"
fi
for dep in $SDK_DEPS; do
    case $dep in
        audiofile) fetch audiofile pinned $GH/adamstark/AudioFile.git 004065d01e9b7338580390d4fdbfbaa46adede4e ;;
        libarchive) fetch libarchive pinned $GH/libarchive/libarchive.git 86c9361fdb4298993f74064b8a0768d07678421e \
            --oss-tree 74824291a5d9f7add7e16cf4e2984217edde2b54 ;;
        zlib) fetch zlib pinned $GH/madler/zlib.git 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf ;;
        openfst) fetch openfst archive https://www.openfst.org/twiki/pub/FST/FstDownload/openfst-1.8.4.tar.gz \
            a8ebbb6f3d92d07e671500587472518cfc87cb79b9a654a5a8abb2d0eb298016 \
            --oss-tree cf59e31962d2ac3b5060bde2f2fc52219f8c19b1 ;;
        dsplib) fetch dsplib pinned $GH/vitalsong/dsplib.git df48e6d81b314738be63ccd9963138ce57f25928 ;;
        catch2) fetch catch2 pinned $GH/catchorg/Catch2.git 182c910b4b63ff587a3440e08f84f70497e49a81 ;;
        kissfft) fetch kissfft pinned $GH/mborgerding/kissfft.git 3050076ab30ed15e4cec8ce68d93f6e5bb82dee3 ;;
        yaml-cpp) fetch yaml-cpp pinned $GH/jbeder/yaml-cpp.git a6bbe0e50ac4074f0b9b44188c28cf00caf1a723 ;;
        eigen) fetch eigen pinned https://gitlab.com/libeigen/eigen.git 1d8b82b0740839c0de7f1242a3585e3390ff5f33 ;;
        jsoncpp) fetch jsoncpp pinned $GH/open-source-parsers/jsoncpp.git 69098a18b9af0c47549d9a271c054d13ca92b006 ;;
        *) echo "Unknown SDK dependency: $dep"; exit 1 ;;
    esac
done

# Our patches to the downloaded repositories (tools/dependencies/patches/<name> to $DOWNLOAD_DIRECTORY/<name>).
# Applied on every run unless already applied: an existing download gets them too. The series is checked as a whole:
# the largest k for which the tree of the clone is exactly HEAD + the first k patches is found, the rest is applied;
# if no prefix matches, the clone was edited by hand or a patch changed: fail without removing anything. Trees are
# compared (write-tree in temporary indexes without split index), not diff texts that depend on the git settings.
tmp_index_git() { GIT_INDEX_FILE="$idx" git -c core.splitIndex=false -C "$dir" "$@"; }
patch_series_tree() { # <directory> <number of patches> <patches...>: tree of HEAD + the first k patches
    local dir=$1 k=$2 idx tree status=0
    shift 2
    idx=$(mktemp) || return 1
    if tmp_index_git read-tree HEAD \
        && { [ "$k" -eq 0 ] || tmp_index_git apply --cached "${@:1:$k}" 2> "$idx.err"; } \
        && tree=$(tmp_index_git write-tree); then
        echo "$tree"
    else
        status=1
        # For the full series show which patch did not apply (shorter prefixes do not fail).
        [ "$k" -eq "$#" ] && [ -s "$idx.err" ] && cat "$idx.err" >&2
    fi
    rm -f "$idx" "$idx.err"
    return $status
}
worktree_tree() { # <directory>: tree of the tracked files of the working copy (via a copy of the index: its stat cache)
    local dir=$1 idx tree status=0
    idx=$(mktemp) || return 1
    if cp "$(git -C "$dir" rev-parse --absolute-git-dir)/index" "$idx" \
        && tmp_index_git add -u && tree=$(tmp_index_git write-tree); then
        echo "$tree"
    else
        status=1
    fi
    rm -f "$idx"
    return $status
}
for patches in "$GIGA_EDGE_ROOT"/tools/dependencies/patches/*/; do
    [ -d "$patches" ] || continue
    dir="$DOWNLOAD_DIRECTORY/$(basename "$patches")"
    [ -d "$dir/.git" ] || continue
    series=("$patches"*.patch)
    [ -e "${series[0]}" ] || continue
    if ! patch_series_tree "$dir" "${#series[@]}" "${series[@]}" > /dev/null; then
        echo "Patches $patches do not apply to $dir (HEAD $(git -C "$dir" rev-parse --short HEAD)): if this is"
        echo "not the pin of download_dependencies.sh, the clone is outdated — remove $DOWNLOAD_DIRECTORY entirely and"
        echo "run the build again; otherwise the patches have to be ported or removed"
        exit 1
    fi
    if ! actual=$(worktree_tree "$dir") || [ -z "$actual" ]; then
        echo "Could not read the state of the clone $dir (git index?)" >&2
        exit 1
    fi
    applied=-1
    for ((k = ${#series[@]}; k >= 0; k--)); do
        if [ "$(patch_series_tree "$dir" "$k" "${series[@]}")" = "$actual" ]; then
            applied=$k
            break
        fi
    done
    if [ "$applied" -lt 0 ]; then
        echo "The changes in $dir do not match the patches $patches (edited by hand or the patch changed):"
        echo "restore the clone (git -C $dir reset --hard && git -C $dir clean -fdx) — the patches will be applied again"
        exit 1
    fi
    if [ "$applied" -lt "${#series[@]}" ]; then
        # The changes of the clone are exactly the applied patches (checked above): the index catches up before
        # --index.
        git -C "$dir" add -u
    fi
    for p in "${series[@]:$applied}"; do
        echo "--- Patch $(basename "$p") to $(basename "$dir") ---"
        if ! git -C "$dir" apply --index "$p"; then
            # the series applies to HEAD (checked above): the working tree is in the way, for example an untracked
            # file the patch creates
            echo "Patch $(basename "$p") does not apply to the working tree of $dir:"
            echo "restore the clone (git -C $dir reset --hard && git -C $dir clean -fdx) — the patches will be applied again"
            exit 1
        fi
    done
done

echo "--- Dependencies downloaded ---"
