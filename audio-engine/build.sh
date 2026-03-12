#!/bin/bash
# Build script for MSYS2 UCRT64 environment
# Prerequisites: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja git

set -e
cd "$(dirname "$0")"

BUILD_DIR=build

if [ "$1" = "clean" ]; then
    rm -rf "$BUILD_DIR"
    echo "Cleaned."
    exit 0
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=gcc \
    -DCMAKE_CXX_COMPILER=g++

ninja -j$(nproc)

echo ""
echo "Build complete: $BUILD_DIR/audio-engine.exe"
