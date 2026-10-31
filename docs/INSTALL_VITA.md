# Installing Fury³ on the PS Vita

You need a homebrew-enabled Vita and your own copy of *Fury³*. The game's art, sound, music and
video are read from your files.

## 1. Install

1. In HENkaku Settings, turn on **Enable Unsafe Homebrew**.
2. Install `fury3.vpk` with VitaShell.
3. For the GPU renderer, install `libshacccg.suprx` at `ur0:data/`. Many Vita ports use this file,
   and the ShaRKBR33D homebrew extracts it from your Vita. Without it the game still runs, using
   its slower software renderer.

## 2. Add your game files

Do one of these:

- Copy the `FURY3` folder from your CD to `ux0:data/fury3/FURY3/`.
- Or put a disc image in `ux0:data/fury3/`: an `.iso`, or the `.bin` of a `.bin`/`.cue` rip. The
  game extracts it on first launch. No PC tools are needed.

The port expects this exact release of the game. These files are required:

| File | Size (bytes) | SHA-1 |
|---|---|---|
| `FURY3.EXE` | 645,120 | `64C42DD37CEDF97CB9F7434C0386896F8F437A93` |
| `SYSTEM\FURY3.POD` | 44,453,636 | `9652ED18B0DE6C980FEA621172262F7D855742D0` |
| `SYSTEM\STARTUP.POD` | 4,194,399 | `C17F337FC753C53C85EA9AF52112C1F6885730EF` |
| `SYSTEM\FURY3.INI` | 971 | `AB01B66E3F9F9A448C6F845D101B7AC8DD899A7A` |
| `SYSTEM\FURY3.INF` | 12,954 | `C6688875434381786A980C4C22999BF6EE1B18BE` |

The 19 `SYSTEM\STORY\*.AVI` cutscenes (62.5 MB) are optional. The game explains on screen if
anything is missing.

## 3. Play

On the title screen:

| Button | Action |
|---|---|
| Start | New game |
| Select | Continue |
| Square | Open the planet list; d-pad up and down picks a planet, left and right a mission, Start begins |

In flight:

| Control | Action |
|---|---|
| Sticks | Flight, roll and throttle |
| D-pad | Weapons and the cockpit display |
| Select | Camera view |
| Start | Pause |
| Square, while paused | Save |

Starting from the planet list does not grant the weapons you would have earned on earlier planets.

## Mods

Replacement models load from `ux0:data/fury3/FURY3/mods/`, listed in a `pod.ini` beside them. The
title screen shows a mods on/off control, and a change takes effect after a restart. See
`tools/mod/glb2fury.py` to convert your own models.

## Problems

Logs and errors are written to `ux0:data/fury3/`. `gpu_log.txt` shows which renderer ran.
