##############################################################################
# DeviceAI SDK — developer setup targets
#
# Usage:
#   make setup                  Run all one-time setup steps
#   make setup-sherpa-android   Download pre-built sherpa-onnx .so files
#   make clean-sherpa-android   Remove downloaded sherpa-onnx Android libs
##############################################################################

COMMONS_DIR := sdk/deviceai-commons
PREBUILT_DIR := $(COMMONS_DIR)/prebuilt/sherpa-onnx

SENTINEL := $(PREBUILT_DIR)/arm64-v8a/libsherpa-onnx-c-api.so

.PHONY: setup setup-sherpa-android clean-sherpa-android help

## Run all one-time developer setup steps
setup: setup-sherpa-android

## Download pre-built sherpa-onnx Android .so files
## Required once before building the speech module for Android.
setup-sherpa-android: $(SENTINEL)

$(SENTINEL):
	@$(COMMONS_DIR)/scripts/download-sherpa-onnx.sh android

## Remove downloaded sherpa-onnx Android libs
clean-sherpa-android:
	@echo "→ Removing sherpa-onnx Android libs..."
	@rm -rf $(PREBUILT_DIR)
	@echo "✓ Done"

## Show available targets
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/## /  /'
