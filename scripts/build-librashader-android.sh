#!/bin/bash
set -e

# Configuration
LIBRASHADER_REPO="https://github.com/SnowflakePowered/librashader.git"
BUILD_DIR="/tmp/librashader_build"
TARGET_ABI="arm64-v8a"
RUST_TARGET="aarch64-linux-android"
# Go to project root
cd "$(dirname "$0")/.."
OUTPUT_DIR="$(pwd)/android/app/src/main/jniLibs/${TARGET_ABI}"

echo "Checking for cargo-ndk..."
if ! command -v cargo-ndk &> /dev/null; then
    echo "cargo-ndk not found, installing..."
    cargo install cargo-ndk
fi

echo "Checking for rustup..."
if ! command -v rustup &> /dev/null; then
    echo "rustup not found. Please install Rust and rustup first."
    exit 1
fi

echo "Adding Rust target ${RUST_TARGET}..."
rustup target add ${RUST_TARGET}

if [ -d "${BUILD_DIR}" ]; then
    echo "Updating existing librashader source..."
    cd "${BUILD_DIR}"
    git pull
else
    echo "Cloning librashader..."
    git clone --depth 1 "${LIBRASHADER_REPO}" "${BUILD_DIR}"
    cd "${BUILD_DIR}"
fi

echo "Building librashader for ${TARGET_ABI}..."
# Build the C-API library
cargo ndk -t ${TARGET_ABI} build --release -p librashader-capi

echo "Deploying .so to ${OUTPUT_DIR}..."
mkdir -p "${OUTPUT_DIR}"
cp "target/${RUST_TARGET}/release/liblibrashader_capi.so" "${OUTPUT_DIR}/liblibrashader.so"

echo "Success! liblibrashader.so has been placed in ${OUTPUT_DIR}"
