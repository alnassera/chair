# CHAIR -- Audio Direction Overlay for VALORANT

Accessibility overlay that converts stereo game audio into visual directional cues for players who are deaf in one ear.

## Quick Start

1. Download `chair-v*.zip` from [Releases](https://github.com/alnassera/chair/releases)
2. Extract and run `chair.exe`
3. Play VALORANT in **Windowed Fullscreen** mode

## What You'll See

- **Ring indicators** around your crosshair showing sound direction (left, right, center)
- **Text labels** on the side the sound came from (`<< STEPS`, `FIRE >>`, etc.)

## Controls

- **Ctrl+Shift+Q** -- close overlay
- **Ctrl+C** in the CHAIR window -- stop everything

## Virtual Audio Cable Setup (Recommended)

By default CHAIR captures all system audio. If you listen to music or use Discord while playing, those sounds can cause false positives. A virtual audio cable isolates VALORANT's audio so CHAIR only hears the game.

### 1. Install VB-CABLE

Download the free virtual audio cable from [vb-audio.com/Cable](https://vb-audio.com/Cable/).

Run the installer and reboot.

### 2. Route VALORANT to the virtual cable

1. Open **Windows Settings > System > Sound**
2. Scroll down and click **Volume mixer**
3. Find **VALORANT** in the app list (the game must be running)
4. Change its **Output device** from your headphones to **CABLE Input (VB-Audio Virtual Cable)**

### 3. Listen to the virtual cable through your headphones

You still need to hear the game. This step routes the cable's output back to your headphones:

1. Open **Control Panel > Sound** (right-click the speaker icon in taskbar > **Sounds**)
2. Go to the **Recording** tab
3. Find **CABLE Output** -- right-click it > **Properties**
4. Go to the **Listen** tab
5. Check **Listen to this device**
6. Set **Playback through this device** to your headphones
7. Click **OK**

### 4. Run CHAIR with the device flag

```
chair.exe --device "CABLE"
```

Now CHAIR only hears VALORANT. Music, Discord, and everything else goes straight to your headphones without interfering.

### Diagram

```
VALORANT audio ──> CABLE Input ──> CABLE Output ──┬──> Your Headphones (Listen)
                                                   └──> CHAIR (captures here)

Discord/Music ────────────────────────────────────────> Your Headphones (default)
```

## Flags

```
chair.exe                  Auto-detect VALORANT, capture all audio
chair.exe --device "CABLE" Capture from a specific audio device
chair.exe --mix            Skip VALORANT detection, capture all audio
chair.exe --log            Save audio clips for debugging
```

## Requirements

- Windows 10/11
- Stereo headphones (disable spatial audio / Dolby Atmos)
- VALORANT in **Windowed Fullscreen** mode (exclusive fullscreen won't show the overlay)

No .NET runtime or other installs needed -- all executables are self-contained.
