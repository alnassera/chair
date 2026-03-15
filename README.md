# CHAIR -- Audio Direction Overlay for VALORANT

Accessibility overlay that converts stereo game audio into visual directional cues for players who are deaf in one ear.

A polar ring around your crosshair spikes outward toward the direction of sounds -- footsteps, gunfire, abilities -- giving you spatial awareness without needing both ears.

## Quick Start

1. Download `chair-v*.zip` from [Releases](https://github.com/alnassera/chair/releases)
2. Extract and follow the **Audio Setup** below
3. Run `chair.exe`
4. Play VALORANT in **Windowed Fullscreen** mode

## What You'll See

A thin circle around your crosshair that spikes outward toward sound sources. Louder sounds produce taller spikes. Multiple simultaneous sounds from different directions show as separate spikes.

## Controls

- **Ctrl+Shift+Q** -- close overlay
- **Ctrl+C** in the CHAIR window -- stop everything

## Audio Setup

CHAIR needs two things:
1. **Stereo audio** from VALORANT routed through a virtual cable (for direction detection)
2. **Mono audio** in your headphones (so you hear everything in your good ear)

### Step 1: Install VB-CABLE

Download the free virtual audio cable from [vb-audio.com/Cable](https://vb-audio.com/Cable/). Run the installer and reboot.

### Step 2: Install Equalizer APO

Download from [sourceforge.net/projects/equalizerapo](https://sourceforge.net/projects/equalizerapo/). During install, the Configurator will ask which devices to apply to -- **check ONLY your headphones**, nothing else. Reboot.

Alternatively, after install run `setup-mono.bat` from the CHAIR folder (included in the release) -- it writes the mono config for you.

### Step 3: Route VALORANT to the virtual cable

1. Open **Windows Settings > System > Sound > Volume Mixer**
2. Find **VALORANT** in the app list (the game must be running)
3. Change its output device to **CABLE Input (VB-Audio Virtual Cable)**
4. Leave all other apps (Discord, Spotify, etc.) on your headphones

### Step 4: Listen to the cable through your headphones

1. Press **Win+R**, type `mmsys.cpl`, press Enter
2. Go to the **Recording** tab
3. Right-click **CABLE Output** > **Properties**
4. Go to the **Listen** tab
5. Check **Listen to this device**
6. Set **Playback through this device** to your headphones
7. Click **OK**

### Step 5: Run CHAIR

```
chair.exe --device "CABLE"
```

### Signal Flow

```
VALORANT ──> CABLE Input (stereo) ──> CABLE Output ──┬──> CHAIR engine (reads stereo)
                                                      └──> Listen loopback ──> Headphones
                                                                                  │
                                                                          Equalizer APO
                                                                          (mono downmix)
                                                                                  │
                                                                              Your ears

Discord/Music ──────────────────────────────────────────────> Headphones (direct)
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
- Stereo headphones (disable spatial audio / Dolby Atmos in Windows sound settings)
- VALORANT in **Windowed Fullscreen** mode (exclusive fullscreen won't show the overlay)
- [VB-CABLE](https://vb-audio.com/Cable/) (free)
- [Equalizer APO](https://sourceforge.net/projects/equalizerapo/) (free, for mono output)

No .NET runtime or other installs needed -- all CHAIR executables are self-contained.

## Troubleshooting

- **No spikes showing**: Check that VALORANT is outputting to CABLE Input in Volume Mixer
- **Hearing nothing**: Make sure Listen is enabled on CABLE Output (mmsys.cpl > Recording tab)
- **Stereo in headphones instead of mono**: Run `setup-mono.bat` or check Equalizer APO config
- **Overlay not visible**: VALORANT must be in Windowed Fullscreen, not Exclusive Fullscreen
- **Too much noise**: The engine filters ambient sounds, but loud music/Discord can leak through. Use `--device CABLE` to isolate VALORANT
