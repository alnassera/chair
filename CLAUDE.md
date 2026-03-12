# CHAIR — Audio Direction Overlay for VALORANT

Accessibility overlay that converts post-mix stereo game audio into visual directional cues for a player who is deaf in one ear.

## Architecture

Two-process design:

- **`audio-engine/`** (C++, MinGW/MSYS2) — WASAPI loopback capture, FFT analysis, onset detection, direction estimation, heuristic classification, named-pipe IPC
- **`overlay-ui/`** (C# WPF, .NET 8) — transparent always-on-top click-through overlay, edge bars + text labels

IPC: named pipe `\\.\pipe\chair-audio-events`, newline-delimited JSON, one-way engine→overlay.

## Toolchain

- **C++ build**: MSYS2 UCRT64 — `mingw-w64-ucrt-x86_64-gcc`, `mingw-w64-ucrt-x86_64-cmake`, `mingw-w64-ucrt-x86_64-ninja`
- **C# build**: .NET 8 SDK
- **Static linking**: audio-engine.exe is fully static (~4MB), only depends on Windows system DLLs
- **Overlay publish**: `dotnet publish --self-contained -p:PublishSingleFile=true` for zero-install distribution

## Build Commands

```bash
# Audio engine (from MSYS2 UCRT64 terminal or with MSYS2 on PATH)
cd audio-engine/build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -Wno-dev
ninja

# Overlay
cd overlay-ui
dotnet build

# Package for distribution
bash package.sh 0.1   # produces dist/chair-v0.1.zip
```

## CLI Flags

```
audio-engine.exe              # auto-detect VALORANT, fall back to full mix
audio-engine.exe --mix        # skip VALORANT detection, capture all audio
audio-engine.exe --log [dir]  # enable clip logging for tuning/debugging
audio-engine.exe --no-pipe    # disable IPC (terminal-only mode)
```

## Key Design Decisions

- **C++ over Rust**: native WASAPI COM access, no FFI overhead, ONNX Runtime C++ API for future ML
- **FFT from M0**: needed for per-band ILD direction and spectral flux onset detection — no throwaway work
- **Per-band onset detection**: 8 log-spaced bands (100Hz–20kHz) with independent flux/debounce — allows simultaneous events from different directions (e.g., gunfire left + footsteps right)
- **Heuristic classifier**: scoring-based on spectral centroid, bandwidth, band energy ratios, attack sharpness — no training data needed
- **Event deduplication**: merges same-class same-direction events within 20ms to reduce noise
- **Process-specific loopback**: auto-detects VALORANT.exe PID, attempts Windows 11 process loopback API, falls back to full mix (Vanguard blocks it)

## Known Issues / Gotchas

- `std::filesystem` crashes with `-static` on MinGW UCRT64 — use Win32 `CreateDirectoryA` instead
- `ActivateAudioInterfaceAsync` not in MinGW import libs — load via `GetProcAddress` from `mmdevapi.dll`
- Process loopback needs STA thread (3s timeout) — Vanguard blocks it anyway, so falls back to full mix
- `signal()` causes access violation in PowerShell — use `SetConsoleCtrlHandler` instead
- `OVERLAPPED` struct for async `ConnectNamedPipe` must persist as class member, not stack local
- Overlay hotkey: **Ctrl+Shift+Q** to close

## Current Status — M3 Complete

### What works
- WASAPI loopback capture (event-driven, 48kHz stereo float32)
- 1024-point FFT, 512 hop (~21ms window, ~10.7ms step)
- Per-band onset detection with adaptive thresholds
- 5-bin ILD direction estimation (hard-left, left, center, right, hard-right)
- Heuristic classifier: STEPS, FIRE, RELOAD, CUE (ABILITY removed — too noisy)
- WPF overlay: edge bars + bottom-right text log, persistent display, fade decay
- Named pipe IPC with auto-reconnect
- Clip logging with WAV export + JSONL metadata
- Auto-detect VALORANT.exe process
- Packaging script for single-zip distribution

### What's next — M4: Calibration and Polish

Per the original plan, M4 focuses on trust and usability:

1. **First-run calibration wizard** — verify L/R channels are correct, set sensitivity
2. **Headset balance adjustment** — compensate if one ear is louder
3. **UI scale/position settings** — let the user move the text log, resize edge bars
4. **Event history strip** — scrolling timeline of recent events (helps build trust)
5. **Diagnostics mode** — show spectral features, confidence scores, band activity in real-time
6. **Threshold tuning** — the classifier thresholds (centroid, bandwidth, attack ratio) need tuning with real VALORANT gameplay clips. Use `--log` to capture clips, review with `python tools/review_clips.py`, adjust `classifier.cpp` scores
7. **Discord filtering** — add voice activity detection to suppress false positives from Discord calls (speech has strong periodicity/harmonic structure, game sounds don't)
8. **Installer / single-exe launcher** — combine `start.bat` into something cleaner

### Open questions
- Exact overlay placement preferences (per-user)
- Whether the ILD thresholds (±2dB / ±6dB) are right for VALORANT's panning model
- Whether Bluetooth headset latency is acceptable
- Whether to support Windows spatial audio modes
- Whether 5 direction bins is the right number or should be 3
