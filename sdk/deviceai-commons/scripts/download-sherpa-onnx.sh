#!/usr/bin/env bash
# Downloads pre-built sherpa-onnx binaries for the specified platform.
#
# Usage:
#   ./download-sherpa-onnx.sh android    # Downloads Android .so files
#
# Files are placed at:
#   sdk/deviceai-commons/prebuilt/sherpa-onnx/${ABI}/libsherpa-onnx-c-api.so
#   sdk/deviceai-commons/prebuilt/sherpa-onnx/${ABI}/libonnxruntime.so

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMMONS_DIR="$(dirname "$SCRIPT_DIR")"

# Load pinned versions
while IFS='=' read -r key value; do
    [[ "$key" =~ ^#.*$ || -z "$key" ]] && continue
    export "$key=$value"
done < "$COMMONS_DIR/VERSIONS"

PLATFORM="${1:-}"
if [[ -z "$PLATFORM" ]]; then
    echo "Usage: $0 <android|ios>"
    exit 1
fi

PREBUILT_DIR="$COMMONS_DIR/prebuilt/sherpa-onnx"
ANDROID_ABIS="arm64-v8a x86_64"

case "$PLATFORM" in
    android)
        ARCHIVE="/tmp/sherpa-onnx-android.tar.bz2"
        URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_ONNX_VERSION}/sherpa-onnx-v${SHERPA_ONNX_VERSION}-android.tar.bz2"

        echo "→ Downloading sherpa-onnx v${SHERPA_ONNX_VERSION} for Android..."
        curl -fL "$URL" -o "$ARCHIVE"

        echo "→ Extracting for ABIs: ${ANDROID_ABIS}..."
        for ABI in $ANDROID_ABIS; do
            mkdir -p "$PREBUILT_DIR/$ABI"
            tar -xjf "$ARCHIVE" \
                --strip-components=3 \
                -C "$PREBUILT_DIR/$ABI" \
                "./jniLibs/$ABI/libsherpa-onnx-c-api.so" \
                "./jniLibs/$ABI/libonnxruntime.so" \
                2>/dev/null || true
        done

        rm -f "$ARCHIVE"
        echo "✓ sherpa-onnx Android libs ready in $PREBUILT_DIR/"
        ;;
    ios)
        echo "iOS pre-built download not yet implemented"
        exit 1
        ;;
    *)
        echo "Unknown platform: $PLATFORM"
        echo "Usage: $0 <android|ios>"
        exit 1
        ;;
esac
