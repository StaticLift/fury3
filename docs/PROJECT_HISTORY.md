# Fury³ on the PS Vita: how the port was made

*A record of the October 2026 effort that took the Fury³ static recompilation
from "boots on Windows" to a playable PS Vita port, with the problems hit along
the way and how each was solved.*

## What Fury³ is, and what was done

*Microsoft Fury³* (1995, developed by Terminal Reality) is a Windows 95 launch-era
arcade flight shooter: eight planets, a 24-mission campaign, voxel terrain, a
software renderer, tracker-module music and AVI cutscenes.

This project does not emulate it. The original 645 KB `FURY3.EXE` is
disassembled and every one of its functions is translated ("lifted") into C:
about 2,000 functions, 233,000 lines of generated C.  That code is compiled for
the target machine and linked against a hand-written runtime that provides what
Windows used to: memory, files, windows and drawing, sound, timers, input. The
result runs natively on Windows, Linux and the PS Vita. Players supply the game
files from their own Fury³ CD. Apart from the title art on its LiveArea screens,
the port contains no art, sound, music or video from the game, though its code
is translated from the original program.

The work started from the [sp00nznet](https://github.com/sp00nznet) *pcrecomp*
toolchain and its Fury³ project, which had the disassembler, lifter and an
early runtime.

## Where it stands

| Platform | Status |
|---|---|
| Windows (Win32 and SDL2 builds) | All 8 planets play; sound and music |
| Linux (GCC, SDL2) | Plays; same frames as Windows |
| PS Vita 1000 | Plays with full controls, sound, music, saving and planet select; about 80 fps in quiet flight and 20–30 in heavy fights on the GPU renderer |

Not done yet: a full campaign play-through on hardware, a clean
install test of the public package, and settling how the Vita program can be
distributed.

## The journey, phase by phase

### 1. Making the translation correct (Windows)

The first goal was a Windows build that plays, because Windows has debuggers
and fast rebuilds. Most problems here were bugs in the translation itself,
which show up far from their cause.

- **A frame pointer reset on every call.** The code generator declared a
  local variable that, through a macro, silently replaced the global `ebp`
  register, setting it to zero at the start of every translated function. Every
  `ebp`-related crash in the project traced back to this one line.
- **"Hose enemy crlf".** The game quit while reading its enemy list. The cause
  was three layers down: part of the C library's text scanner falls through
  into the next function in the original binary, and the translator emitted a
  `return` there instead of continuing. The scanner's caller then got the wrong
  register back and read from a dead file. Fixing the translator fixed 61 such
  sites at once.
- **Function boundaries.** The disassembler marks some addresses as "aliases"
  (labels inside another function). Excluding them all broke 792 legitimate
  jump targets; a small script now classifies each one by who jumps to it.
- **The title screen showing during flight.** The runtime guessed which image
  to display. Modelling exactly what Windows does (the game draws into an
  off-screen device context and copies it to the window) fixed it for good.
- **Sound.** The game mixes its own audio in a background thread that the
  runtime was not running, and one audio call had the wrong argument count.
  With both fixed, sound effects and the music came back. The music turned out
  to be the game's own tracker-module player, not CD audio as first assumed.

### 2. Making it fast

The first working build ran at about 14 fps on a laptop. Profiling found that
85% of the time went to looking up environment variables (debug switches)
inside the hottest code paths. Caching them, removing redundant sleeps, and
generating code that keeps CPU registers in local variables took flight to
1.6–3.5 ms of CPU per frame.

### 3. Making it portable

All operating-system services moved behind one small interface with Windows,
POSIX and Vita implementations, and SDL2 took over windowing, audio and input.
A Linux build with GCC came first, as a cheap check for portability bugs, then
the Vita build with VitaSDK.

### 4. The Vita

The first Vita run played, with working controls, at about 20 fps, dropping to
3–4 fps in explosions and stalling for 30 seconds while loading. Each fix was
measured on the device:

- **Loading stalls:** every file-system call on the Vita's memory card is
  slow, and the game re-opens its archive for every asset (628 opens per
  mission). An index of the game folder built at startup, a cache of path
  lookups, pooled file handles and a 64 KB read window cut the stall to a few
  seconds.
- **CPU clocks** raised to 444 MHz, the floating-point register stack turned
  into a sliding window, and a packaging failure in the Vita's ELF converter
  fixed with a small linker script.
- **The renderer:** the two hottest routines, hand-written assembly texture
  mappers, were rewritten in native C. To be sure the rewrite was exact, a
  verification mode runs both versions on identical copies of the entire game
  memory and compares every byte, register and flag: 0 differences in 1,600
  sampled calls (400 of each routine on each of two planets). Busy scenes went from about 14 fps to mostly 20.

### 5. Making it a Vita game

- **Controls:** the game's key handling was read from its code to find what
  each key does, then mapped to the Vita: sticks for flight, roll and throttle;
  d-pad for weapons and the cockpit display; Select for the camera view; Start
  to pause.
- **Saving:** saving had never worked in any build. The game asks Windows for a
  "Save As" dialog, which did nothing. It now saves to a fixed slot; d-pad down
  saves while paused, and Select on the title screen continues. Fixing this
  also uncovered that keyboard shortcuts ignored the Ctrl key.
- **Title screen:** a test helper had been pressing New Game automatically. The
  title now waits, with on-screen "START New Game / SELECT Continue" prompts.
- **LiveArea:** bubble icon, start gate and background made from the game's
  own title art.

### 6. Preparing for release

The port must run from the player's own, untouched CD files. Testing exactly
that exposed two hidden bugs:

- **The game could not read its own archives.** Early on, the game's archive
  lookup had been replaced by a shortcut that only checked loose files on
  disk, and the archives had been unpacked to compensate. Without the unpacked
  files, models loaded with garbage sizes. Removing the shortcut and letting
  the original code run fixed it; the game now reads its archives like the
  original.
- **New Game hung when the cutscene files were present.** The game waits for
  Windows to say a movie has finished. The runtime now answers the movie
  commands and reports each movie finished, which is also where real cutscene
  playback can be added.

On first launch the Vita port finds the game folder, or extracts it from a
disc image (`.iso`, or the `.bin` of a bin/cue rip) placed on the memory card,
checks `FURY3.EXE` against the exact version that was translated, and explains
on screen what is wrong if anything is missing.

### 7. A GPU renderer

The game draws into a 320×200 frame in memory, so the Vita's GPU sat idle. The
runtime now catches polygons at the game's own drawing routines and sends them
to vitaGL, with a shader doing the maths of the game's shade tables and palette.
2D work such as the HUD stays in the original frame and is layered around them.
It needs the system's `libshacccg.suprx`; without that file the game uses
software rendering. The first Vita run reached 80 fps in quiet flight and 20–30
in fights.

- **Shader compile crashes:** vitaGL compiles at link time and crashes if that
  fails. The compiler only worked once its allocators used the C heap and the
  heap grew to 96 MB. Which of the two mattered is unknown.
- **Sorting:** the game's depth values only sort a model against itself, and it
  clears them before each model. One GPU depth buffer put the player's ship
  behind distant buildings. Each model now gets its own slice of depth. The
  first attempt also clipped the ground, showing grey boxes, so only
  depth-tested polygons use the slices.
- **Colour:** blended laser spans carry a palette index and needed their own
  shader mode. Radar blips were being covered and now draw on a top layer.

### 8. Mods and a planet list

`tools/mod/glb2fury.py` converts a 3D model into a game model, using the game's
own `pod.ini` archive override. The model format was read from the code, which
opened up baked 256×256 textures, moving wings, engine glow tied to the
throttle, animated walkers and an automatic low-detail version. Low-detail
models cut a test mission from 1,442 polygons a frame to 775. Stock models are
untouched, and mods can be switched off on the title screen. Replacements must
be scaled by hit area as well as length: a fighter under about 60% of the
original's width is very hard to hit.

Square on the title screen opens a list of the eight planets and their missions.

### 9. Bugs only a full play test finds

- **The first boss could not be hurt.** Its shields fell but its health never
  moved. A log added to the Vita build confirmed the shields were down, and a
  direct test showed each hit spot had a size of zero. The translated `sscanf`
  handed its delimiter back through a replaced file function, lost it, and
  stopped after the first number. `sscanf` now uses the runtime's own reader.
- **Retry Mission (Y/N) was a dead end.** It waits for a typed character that
  neither the Vita nor the SDL builds ever sent, then returns to the title.
  Cross or Start now retries and Circle quits.

## What the player supplies

From the `FURY3` folder of the Fury³ CD:

| File | Size (bytes) | SHA-1 |
|---|---|---|
| `FURY3.EXE` | 645,120 | `64C42DD37CEDF97CB9F7434C0386896F8F437A93` |
| `SYSTEM\FURY3.POD` | 44,453,636 | `9652ED18B0DE6C980FEA621172262F7D855742D0` |
| `SYSTEM\STARTUP.POD` | 4,194,399 | `C17F337FC753C53C85EA9AF52112C1F6885730EF` |
| `SYSTEM\FURY3.INI` | 971 | `AB01B66E3F9F9A448C6F845D101B7AC8DD899A7A` |
| `SYSTEM\FURY3.INF` | 12,954 | `C6688875434381786A980C4C22999BF6EE1B18BE` |
| `SYSTEM\STORY\*.AVI` (19 files, optional) | 62.5 MB total | |

## Lessons

- Fix root causes. The two bugs found at release time were both hidden by
  early shortcuts.
- Get it correct on a PC first; measure on the real device before optimising.
- Build the tools that answer questions: scripted key presses, periodic
  screenshots, a per-function profiler, and a byte-exact A/B harness for
  rewrites.
- Keep a running log of every root cause. `SESSION_HANDOFF.md` is that log for
  this project.

## Credits

*Fury³* © 1995 Microsoft Corporation and Terminal Reality, Inc. This project
contains no game data. Built on the sp00nznet pcrecomp toolchain, with VitaSDK
and SDL2.
