#!/bin/bash
# Package CHAIR for distribution.
# Run from: MSYS2 UCRT64 terminal (or Git Bash if MSYS2 is on PATH)
# Produces: dist/chair-v<VERSION>.zip

set -e
cd "$(dirname "$0")"

VERSION="${1:-0.1}"
DIST="dist/chair"
ZIP="dist/chair-v${VERSION}.zip"

export PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH"

echo "=== Building audio-engine ==="
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
echo "=== Building overlay (self-contained) ==="
cd overlay-ui
"/c/Program Files/dotnet/dotnet.exe" publish -c Release -r win-x64 \
    --self-contained \
    -p:PublishSingleFile=true \
    -p:IncludeNativeLibrariesForSelfExtract=true \
    -p:DebugType=none \
    -o ../dist/_overlay
cd ..

echo ""
echo "=== Packaging ==="
rm -rf "$DIST" "$ZIP"
mkdir -p "$DIST"

echo ""
echo "=== Building launcher ==="
gcc -O2 -o launcher/chair.exe launcher/main.c -static

cp audio-engine/build/audio-engine.exe "$DIST/"
cp dist/_overlay/chair-overlay.exe "$DIST/"
cp launcher/chair.exe "$DIST/"

cat > "$DIST/README.txt" << 'EOF'
CHAIR - Audio Direction Overlay for VALORANT
=============================================

Helps you see where sounds are coming from.

HOW TO USE:
1. Double-click "chair.exe" — it launches everything for you
2. Play VALORANT (windowed or borderless fullscreen)
3. You'll see:
   - Amber bars on screen edges showing sound direction
   - Text labels on the side the sound came from (left or right)

CONTROLS:
- Ctrl+Shift+Q : close the overlay
- Ctrl+C in the CHAIR window to stop everything

NOTES:
- Works best with stereo headphones (no spatial audio / Dolby Atmos)
- The overlay is click-through, it won't interfere with your game
- If sounds seem backwards, your L/R channels may be swapped

FLAGS (advanced — pass to chair.exe):
  chair.exe --mix       Capture all audio (skip VALORANT detection)
  chair.exe --log       Save audio clips for debugging
EOF

# Zip it
rm -rf dist/_overlay
cd dist
if command -v 7z &>/dev/null; then
    7z a -tzip "../$ZIP" chair/
elif command -v zip &>/dev/null; then
    zip -r "../$ZIP" chair/
else
    # Fallback: use PowerShell
    powershell -Command "Compress-Archive -Path 'chair' -DestinationPath '../$ZIP' -Force"
fi
cd ..

rm -rf "$DIST"

SIZE=$(ls -lh "$ZIP" | awk '{print $5}')
echo ""
echo "=== Done ==="
echo "Package: $ZIP ($SIZE)"
echo "Give this zip to your friend."
