#!/bin/bash
# Package CHAIR for distribution.
# Run from: MSYS2 UCRT64 terminal (or Git Bash if MSYS2 is on PATH)
# Produces: dist/chair-v<VERSION>.zip

set -e
cd "$(dirname "$0")"

VERSION="${1:-1.0}"
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
echo "=== Building launcher ==="
gcc -O2 -o launcher/chair.exe launcher/main.c -static

echo ""
echo "=== Packaging ==="
rm -rf "$DIST" "$ZIP"
mkdir -p "$DIST"

cp audio-engine/build/audio-engine.exe "$DIST/"
cp dist/_overlay/chair-overlay.exe "$DIST/"
cp launcher/chair.exe "$DIST/"
cp setup-mono.bat "$DIST/"

cat > "$DIST/README.txt" << 'EOF'
CHAIR v1.0 - Audio Direction Overlay for VALORANT
==================================================

Visual sound radar for players who are deaf in one ear.
A ring around your crosshair spikes toward the direction of sounds.

FIRST-TIME SETUP:
1. Install VB-CABLE: https://vb-audio.com/Cable/
2. Install Equalizer APO: https://sourceforge.net/projects/equalizerapo/
   - During install, check ONLY your headphones in the Configurator
   - Reboot
3. Double-click "setup-mono.bat" to configure mono headphone output
4. In Windows Volume Mixer, set VALORANT output to "CABLE Input"
5. In mmsys.cpl (Win+R) > Recording > CABLE Output > Properties > Listen:
   - Check "Listen to this device" > select your headphones

RUNNING:
  chair.exe --device "CABLE"

CONTROLS:
  Ctrl+Shift+Q : close the overlay
  Ctrl+C       : stop everything

FLAGS:
  chair.exe                  Auto-detect VALORANT, capture all audio
  chair.exe --device "CABLE" Capture from virtual cable (recommended)
  chair.exe --mix            Skip VALORANT detection, capture all audio
  chair.exe --log            Save audio clips for debugging

REQUIREMENTS:
  - Windows 10/11
  - Stereo headphones (disable spatial audio / Dolby Atmos)
  - VALORANT in Windowed Fullscreen mode
  - VB-CABLE (free)
  - Equalizer APO (free)

Full docs: https://github.com/alnassera/chair
EOF

# Zip it
rm -rf dist/_overlay
cd dist
if command -v 7z &>/dev/null; then
    7z a -tzip "../$ZIP" chair/
elif command -v zip &>/dev/null; then
    zip -r "../$ZIP" chair/
else
    powershell -Command "Compress-Archive -Path 'chair' -DestinationPath '../$ZIP' -Force"
fi
cd ..

rm -rf "$DIST"

SIZE=$(ls -lh "$ZIP" | awk '{print $5}')
echo ""
echo "=== Done ==="
echo "Package: $ZIP ($SIZE)"
