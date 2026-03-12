#!/usr/bin/env python3
"""
CHAIR clip review tool.

Usage:
    python review_clips.py <session_dir>
    python review_clips.py clips/session_2026-03-10_02-30-00

Reads events.jsonl from the session directory, lets you play each clip
and label it. Labels are saved to labels.jsonl in the same directory.
"""

import json
import sys
import os
import subprocess
from pathlib import Path

# Sound classes for labeling
CLASSES = [
    "footsteps",
    "gunfire",
    "reload",
    "ability",
    "global_cue",
    "ui_sound",
    "unknown",
    "noise",       # false positive / not a real event
]

def play_clip(wav_path: str):
    """Play a WAV file using the system default player."""
    if sys.platform == "win32":
        # Use PowerShell to play synchronously without opening a window
        subprocess.run(
            ["powershell", "-c",
             f"(New-Object Media.SoundPlayer '{wav_path}').PlaySync()"],
            capture_output=True
        )
    elif sys.platform == "darwin":
        subprocess.run(["afplay", wav_path], capture_output=True)
    else:
        subprocess.run(["aplay", wav_path], capture_output=True)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    session_dir = Path(sys.argv[1])
    events_path = session_dir / "events.jsonl"
    labels_path = session_dir / "labels.jsonl"

    if not events_path.exists():
        print(f"Error: {events_path} not found")
        sys.exit(1)

    # Load events
    clips = []
    with open(events_path) as f:
        for line in f:
            line = line.strip()
            if line:
                clips.append(json.loads(line))

    # Load existing labels
    existing_labels = {}
    if labels_path.exists():
        with open(labels_path) as f:
            for line in f:
                line = line.strip()
                if line:
                    label = json.loads(line)
                    existing_labels[label["clip"]] = label

    print(f"Session: {session_dir}")
    print(f"Clips: {len(clips)} ({len(existing_labels)} already labeled)")
    print(f"\nClasses: {', '.join(f'{i}={c}' for i, c in enumerate(CLASSES))}")
    print(f"Commands: [0-{len(CLASSES)-1}]=label  p=play again  s=skip  q=quit\n")

    new_labels = dict(existing_labels)

    for i, clip_data in enumerate(clips):
        clip_name = clip_data["clip"]
        wav_path = session_dir / clip_name
        events = clip_data.get("events", [])
        num_events = len(events)

        # Summary of events in this clip
        dirs = [e["dir"] for e in events]
        bands = [f'{e["freqLo"]:.0f}-{e["freqHi"]:.0f}' for e in events]
        db_range = f'{min(e["db"] for e in events):.0f} to {max(e["db"] for e in events):.0f}'

        existing = existing_labels.get(clip_name)
        label_str = f"  [labeled: {existing['label']}]" if existing else ""

        print(f"--- Clip {i+1}/{len(clips)}: {clip_name}{label_str} ---")
        print(f"  Events: {num_events}  Dirs: {', '.join(dirs)}  Bands: {', '.join(set(bands))}  dB: {db_range}")

        if not wav_path.exists():
            print(f"  WARNING: {wav_path} not found, skipping")
            continue

        # Auto-play
        play_clip(str(wav_path))

        while True:
            choice = input("  > ").strip().lower()

            if choice == "q":
                save_labels(labels_path, new_labels)
                print(f"\nSaved {len(new_labels)} labels to {labels_path}")
                return
            elif choice == "s":
                break
            elif choice == "p":
                play_clip(str(wav_path))
            elif choice.isdigit() and 0 <= int(choice) < len(CLASSES):
                label = CLASSES[int(choice)]
                new_labels[clip_name] = {
                    "clip": clip_name,
                    "label": label,
                    "events": events,
                }
                print(f"  -> {label}")
                break
            else:
                print(f"  Unknown command. [0-{len(CLASSES)-1}] p s q")

    save_labels(labels_path, new_labels)
    print(f"\nDone! {len(new_labels)} labels saved to {labels_path}")


def save_labels(path: Path, labels: dict):
    with open(path, "w") as f:
        for label in labels.values():
            f.write(json.dumps(label) + "\n")


if __name__ == "__main__":
    main()
