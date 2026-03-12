#!/bin/bash
# CHAIR — build everything
# Run from: MSYS2 UCRT64 terminal
#
# Usage:
#   bash build.sh          # build all
#   bash build.sh engine   # audio engine only
#   bash build.sh overlay  # overlay only
#   bash build.sh launcher # launcher only
#   bash build.sh clean    # remove build artifacts

set -e
cd "$(dirname "$0")"

export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"

build_engine() {
    echo "=== Building audio engine ==="
    cd audio-engine
    mkdir -p build && cd build
    cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_CXX_COMPILER=g++ \
        -Wno-dev
    ninja -j$(nproc)
    cd ../..
    echo ""
}

build_overlay() {
    echo "=== Building overlay ==="
    cd overlay-ui
    "/c/Program Files/dotnet/dotnet.exe" build -c Release -v quiet
    cd ..
    echo ""
}

build_launcher() {
    echo "=== Building launcher ==="
    gcc -O2 -o launcher/chair.exe launcher/main.c -static
    echo "  -> launcher/chair.exe"
    echo ""
}

case "${1:-all}" in
    engine)   build_engine ;;
    overlay)  build_overlay ;;
    launcher) build_launcher ;;
    clean)
        echo "Cleaning..."
        rm -rf audio-engine/build overlay-ui/bin overlay-ui/obj launcher/chair.exe dist
        echo "Done." ;;
    all)
        build_engine
        build_overlay
        build_launcher
        echo "=== All built ===" ;;
    *)
        echo "Usage: bash build.sh [engine|overlay|launcher|clean|all]"
        exit 1 ;;
esac
