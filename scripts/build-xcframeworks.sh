#!/usr/bin/env bash
# =============================================================================
# build-xcframeworks.sh
#
# Builds CLlama.xcframework (llama.cpp) and CWhisper.xcframework (whisper.cpp)
# for the DeviceAI Swift SDK.
#
# Usage:
#   ./scripts/build-xcframeworks.sh            # build both
#   ./scripts/build-xcframeworks.sh --llm-only
#   ./scripts/build-xcframeworks.sh --speech-only
#
# Output: swift/Binaries/CLlama.xcframework
#         swift/Binaries/CWhisper.xcframework
#
# After running, uncomment the binary targets in swift/Package.swift and add
# CLlama/CWhisper as dependencies to DeviceAiLlm/DeviceAiStt.
# =============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLAMA_DIR="$REPO_ROOT/llama.cpp"
WHISPER_DIR="$REPO_ROOT/whisper.cpp"
BUILD_DIR="$REPO_ROOT/.build-xcframeworks"
OUT_DIR="$REPO_ROOT/swift/Binaries"
IOS_MIN="16.0"
JOBS=$(sysctl -n hw.ncpu)

# ── Colours ──────────────────────────────────────────────────────────────────
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
log()  { echo -e "${GREEN}==> ${NC}$1"; }
warn() { echo -e "${YELLOW}warn:${NC} $1"; }
die()  { echo -e "${RED}error:${NC} $1" >&2; exit 1; }

# ── Prerequisites ────────────────────────────────────────────────────────────
command -v cmake      >/dev/null 2>&1 || die "cmake not found  →  brew install cmake"
command -v xcodebuild >/dev/null 2>&1 || die "xcodebuild not found  →  install Xcode"
command -v libtool    >/dev/null 2>&1 || die "libtool not found"

log "Initialising submodules..."
cd "$REPO_ROOT"
git submodule update --init llama.cpp whisper.cpp

[[ -f "$LLAMA_DIR/CMakeLists.txt" ]]   || die "llama.cpp not found at $LLAMA_DIR"
[[ -f "$WHISPER_DIR/CMakeLists.txt" ]] || die "whisper.cpp not found at $WHISPER_DIR"

mkdir -p "$BUILD_DIR" "$OUT_DIR"

# ── Helper: cmake configure + build ─────────────────────────────────────────
# Args: label src_dir build_dir sdk arch extra_cmake_flags...
cmake_build() {
    local label="$1" src="$2" bld="$3" sdk="$4" arch="$5"
    shift 5

    local sysroot
    sysroot=$(xcrun --sdk "$sdk" --show-sdk-path)

    log "  cmake $label ($arch / $sdk)..."
    rm -rf "$bld"
    mkdir -p "$bld"

    cmake -S "$src" -B "$bld" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES="$arch" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        "$@" \
        > "$bld/cmake-configure.log" 2>&1 \
        || { cat "$bld/cmake-configure.log"; die "cmake configure failed for $label"; }

    cmake --build "$bld" --config Release -- -j"$JOBS" \
        > "$bld/cmake-build.log" 2>&1 \
        || { cat "$bld/cmake-build.log"; die "cmake build failed for $label"; }
}

# ── Helper: merge all .a files in a build tree into one fat archive ──────────
merge_libs() {
    local build_dir="$1" out="$2"
    local libs
    libs=$(find "$build_dir" -name "*.a" ! -name "$(basename "$out")" | tr '\n' ' ')
    [[ -n "$libs" ]] || die "No .a files found in $build_dir"
    # shellcheck disable=SC2086
    libtool -static -o "$out" $libs 2>/dev/null \
        || libtool -static -o "$out" $libs
}

# ── Helper: find header (checks include/ then root) ──────────────────────────
find_header() {
    local dir="$1" name="$2"
    if [[ -f "$dir/include/$name" ]]; then
        echo "$dir/include/$name"
    elif [[ -f "$dir/$name" ]]; then
        echo "$dir/$name"
    else
        die "Cannot find $name in $dir or $dir/include/"
    fi
}

# ── Helper: write module.modulemap ───────────────────────────────────────────
write_modulemap() {
    local module="$1" header="$2" dir="$3"
    cat > "$dir/module.modulemap" <<EOF
module ${module} {
    header "${header}"
    export *
}
EOF
}

# ── Helper: stage headers for one slice ──────────────────────────────────────
stage_headers() {
    local dest="$1" module="$2" header_path="$3"
    local header_name
    header_name=$(basename "$header_path")
    mkdir -p "$dest"
    cp "$header_path" "$dest/"
    write_modulemap "$module" "$header_name" "$dest"
}

# =============================================================================
#  CLlama.xcframework  (llama.cpp → LLM inference)
# =============================================================================
build_cllama() {
    log "Building CLlama.xcframework..."

    # Metal is disabled here so the library compiles without needing an
    # embedded .metallib. For Metal-accelerated inference, enable it and
    # bundle the metallib separately (advanced — see llama.cpp docs).
    local FLAGS=(
        -DLLAMA_BUILD_TESTS=OFF
        -DLLAMA_BUILD_EXAMPLES=OFF
        -DLLAMA_BUILD_SERVER=OFF
        -DGGML_METAL=OFF          # CPU-only for XCFramework; Metal requires .metallib bundling
        -DGGML_ACCELERATE=ON      # Apple Accelerate BLAS — fast CPU matmul
        -DGGML_BLAS=OFF
    )

    cmake_build "CLlama device"     "$LLAMA_DIR" "$BUILD_DIR/llama-dev"     iphoneos         arm64   "${FLAGS[@]}"
    cmake_build "CLlama sim arm64"  "$LLAMA_DIR" "$BUILD_DIR/llama-sim-a64" iphonesimulator  arm64   "${FLAGS[@]}"
    cmake_build "CLlama sim x86_64" "$LLAMA_DIR" "$BUILD_DIR/llama-sim-x64" iphonesimulator  x86_64  "${FLAGS[@]}"

    log "  Merging CLlama archives..."
    merge_libs "$BUILD_DIR/llama-dev"     "$BUILD_DIR/CLlama-dev.a"
    merge_libs "$BUILD_DIR/llama-sim-a64" "$BUILD_DIR/CLlama-sim-a64.a"
    merge_libs "$BUILD_DIR/llama-sim-x64" "$BUILD_DIR/CLlama-sim-x64.a"

    lipo -create \
        "$BUILD_DIR/CLlama-sim-a64.a" \
        "$BUILD_DIR/CLlama-sim-x64.a" \
        -output "$BUILD_DIR/CLlama-sim.a"

    local header
    header=$(find_header "$LLAMA_DIR" "llama.h")
    stage_headers "$BUILD_DIR/CLlama-hdr-dev" "CLlama" "$header"
    stage_headers "$BUILD_DIR/CLlama-hdr-sim" "CLlama" "$header"

    log "  Packaging CLlama.xcframework..."
    rm -rf "$OUT_DIR/CLlama.xcframework"
    xcodebuild -create-xcframework \
        -library "$BUILD_DIR/CLlama-dev.a"    -headers "$BUILD_DIR/CLlama-hdr-dev" \
        -library "$BUILD_DIR/CLlama-sim.a"    -headers "$BUILD_DIR/CLlama-hdr-sim" \
        -output  "$OUT_DIR/CLlama.xcframework"

    log "CLlama.xcframework ✓"
}

# =============================================================================
#  CWhisper.xcframework  (whisper.cpp → speech-to-text)
# =============================================================================
build_cwhisper() {
    log "Building CWhisper.xcframework..."

    local FLAGS=(
        -DWHISPER_BUILD_TESTS=OFF
        -DWHISPER_BUILD_EXAMPLES=OFF
        -DGGML_METAL=OFF
        -DGGML_ACCELERATE=ON
        -DWHISPER_COREML=OFF      # CoreML requires a separate .mlmodel bundle
    )

    cmake_build "CWhisper device"     "$WHISPER_DIR" "$BUILD_DIR/whisper-dev"     iphoneos         arm64   "${FLAGS[@]}"
    cmake_build "CWhisper sim arm64"  "$WHISPER_DIR" "$BUILD_DIR/whisper-sim-a64" iphonesimulator  arm64   "${FLAGS[@]}"
    cmake_build "CWhisper sim x86_64" "$WHISPER_DIR" "$BUILD_DIR/whisper-sim-x64" iphonesimulator  x86_64  "${FLAGS[@]}"

    log "  Merging CWhisper archives..."
    merge_libs "$BUILD_DIR/whisper-dev"     "$BUILD_DIR/CWhisper-dev.a"
    merge_libs "$BUILD_DIR/whisper-sim-a64" "$BUILD_DIR/CWhisper-sim-a64.a"
    merge_libs "$BUILD_DIR/whisper-sim-x64" "$BUILD_DIR/CWhisper-sim-x64.a"

    lipo -create \
        "$BUILD_DIR/CWhisper-sim-a64.a" \
        "$BUILD_DIR/CWhisper-sim-x64.a" \
        -output "$BUILD_DIR/CWhisper-sim.a"

    local header
    header=$(find_header "$WHISPER_DIR" "whisper.h")
    stage_headers "$BUILD_DIR/CWhisper-hdr-dev" "CWhisper" "$header"
    stage_headers "$BUILD_DIR/CWhisper-hdr-sim" "CWhisper" "$header"

    log "  Packaging CWhisper.xcframework..."
    rm -rf "$OUT_DIR/CWhisper.xcframework"
    xcodebuild -create-xcframework \
        -library "$BUILD_DIR/CWhisper-dev.a"    -headers "$BUILD_DIR/CWhisper-hdr-dev" \
        -library "$BUILD_DIR/CWhisper-sim.a"    -headers "$BUILD_DIR/CWhisper-hdr-sim" \
        -output  "$OUT_DIR/CWhisper.xcframework"

    log "CWhisper.xcframework ✓"
}

# =============================================================================
#  Main
# =============================================================================
BUILD_LLAMA=true
BUILD_WHISPER=true

for arg in "$@"; do
    case "$arg" in
        --llm-only)    BUILD_WHISPER=false ;;
        --speech-only) BUILD_LLAMA=false ;;
        --help|-h)
            echo "Usage: $0 [--llm-only | --speech-only]"
            exit 0 ;;
    esac
done

$BUILD_LLAMA   && build_cllama
$BUILD_WHISPER && build_cwhisper

echo ""
log "All done!  Output: swift/Binaries/"
echo ""
echo "Next steps:"
echo "  1. In swift/Package.swift, uncomment the .binaryTarget lines for CLlama/CWhisper"
echo "  2. Add \"CLlama\" to DeviceAiLlm dependencies"
echo "  3. Add \"CWhisper\" to DeviceAiStt dependencies"
echo "  4. In LlamaEngine.swift / WhisperEngine.swift, uncomment the real C calls"
echo "  5. Clean DerivedData and rebuild"
