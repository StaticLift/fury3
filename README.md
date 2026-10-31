# Fury³ — Static Recompilation

```
   ███████╗██╗   ██╗██████╗ ██╗   ██╗██████╗
   ██╔════╝██║   ██║██╔══██╗╚██╗ ██╔╝╚════██╗
   █████╗  ██║   ██║██████╔╝ ╚████╔╝  █████╔╝
   ██╔══╝  ██║   ██║██╔══██╗  ╚██╔╝   ╚═══██╗
   ██║     ╚██████╔╝██║  ██║   ██║   ██████╔╝
   ╚═╝      ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚═════╝
        outrun the fury · 1995 · reborn
```

**A from-the-binary static recompilation of _Fury³_ (1995) — Microsoft's Windows 95
launch-era arcade flight shooter — into clean, native, modern C.**

No DOSBox. No Win32s. No emulation. We disassemble the original 645 KB executable,
lift it to C, shim its handful of OS calls onto SDL2, and rebuild it as a real
program you can compile and run on Windows 11, Linux, or a Raspberry Pi.

> Part of the [pcrecomp](https://github.com/sp00nznet) family of PC static-recompilation
> projects (civ · elfish · gta · recoil · crimsonskies · black & white · …).

---

## It runs — from title to flying the canyon

The recompiled binary boots straight through Microsoft's original front-end and into
the mission. These are captured live from the recompiled `FURY3.EXE` (no emulator):

| New Game / title | Mission briefing | Enemy tactical intros |
|:---:|:---:|:---:|
| ![Title screen](docs/screenshots/1-menu.png) | ![Mission briefing](docs/screenshots/2-briefing.png) | ![Enemy tactical intro](docs/screenshots/3-enemy-intros.png) |
| The animated *Microsoft Fury³* title. | The 3D briefing — starfield, the rotating Bion sphere, story crawl. | Per-enemy tactical briefings with the live 3D scene. |

| Fly-in | Flying the canyon |
|:---:|:---:|
| ![Fly-in to the canyon](docs/screenshots/4-flyin.png) | ![Flying the canyon](docs/screenshots/5-canyon-flight.png) |
| The approach into the level — the voxel landscape. | Live flight: textured terrain, water, and the full HUD (objective, distance, weapon, sector, shield bar, radar). |

The full pipeline runs natively: **boot → title → New Game → 3D briefing → enemy
tactical intros → fly-in → flying the canyon**, all rendered through the original
voxel/span rasterizer with shading, textures and HUD.

---

## Modern frontend — SDL2 + Dear ImGui menu

An optional SDL2 + [Dear ImGui](https://github.com/ocornut/imgui) frontend (build with
`-DFURY3_SDL=ON`, or `bash build_sdl.sh`) wraps the game in a resizable window with an
in-game menu bar — **File** (New Game · Save/Load State · settings), **Video** (scale ·
nearest/linear filter · v-sync · fullscreen · scanlines), **Audio** (volume · mute), and
**Controls**, all persisted to `fury3_config.ini`. Press **F1** in-game to toggle it.

| Menu over the game | Controls — keyboard + Xbox-controller binding |
|:---:|:---:|
| ![Menu bar](docs/screenshots/6-menu.png) | ![Controls binding](docs/screenshots/7-menu-controls.png) |

The **Controls** panel binds every flight action — pitch, roll, yaw, throttle, fire,
afterburner, slide — to a **keyboard key and an Xbox-style gamepad button** (click a cell,
press the input; analog-stick option + dead-zone for pitch/roll). Real joysticks/gamepads
also work directly: the WINMM `joyGetPos` path is wired to the host's real joystick, so a
physical flight stick drives the analog controls. (The plain Win32/GDI build remains the
default; the SDL frontend is opt-in.)

---

## Why Fury³?

Fury³ was Microsoft's flagship game for the Windows 95 era — a six-degrees-of-freedom
canyon-blasting shooter built by **Terminal Reality** on the same voxel-landscape
"fury" engine that powered _Terminal Velocity_ and later _Hellbender_. It came on the
disc that taught a generation of PCs what "multimedia" meant. Then Win32s, GDI, and
`waveOut` aged out from under it, and it became a pain to run.

But here's the thing that makes it a *perfect* recompilation target — we cracked open
the binary and it is **astonishingly clean**:

| | |
|---|---|
| **Format** | PE32, i386, Windows GUI |
| **Size** | 645,120 bytes — a single self-contained `FURY3.EXE` |
| **Code** | ~383 KB in `.text` (one `.text` section, no packing, no DRM) |
| **Linker** | Microsoft Link 2.55 (1995) — early, predictable codegen |
| **Built** | 1995-08-14 — three weeks before Windows 95 shipped |
| **OS surface** | **160 imports across just 5 DLLs** |

And those five DLLs are the whole story:

```
GDI32     →  CreateDIBSection + StretchBlt          the entire renderer is a
             SetDIBColorTable + CreatePalette        software rasterizer that draws
                                                      into a memory DIB, then blits it
USER32    →  window, message loop, keyboard/mouse    input + window
KERNEL32  →  file I/O, memory, GetPrivateProfile*    files + the .INI config
WINMM     →  waveOut* (raw PCM streaming)            audio
comdlg32  →  GetOpenFileName  (3 calls)              load/save dialog
```

There is **no DirectDraw, no WinG, no DCI, no DirectSound** — nothing exotic. The 3D
engine renders the world into an off-screen 8-bit DIB and `StretchBlt`s it to the
window. That means the *entire* operating-system dependency of this game maps onto a
few hundred lines of SDL2:

- the DIB framebuffer → an `SDL_Texture` we upload each frame
- the palette → `SDL_SetPaletteColors`
- `waveOut` PCM → `SDL_AudioStream`
- the message loop → `SDL_PollEvent`

The hard part of the game — the renderer, the physics, the AI, the mission logic —
is *self-contained 386 code with no OS entanglement*. That's exactly what static
recompilation eats for breakfast.

---

## Status

🟢 **Playable — boots to flight.** The recompiled binary runs the whole pipeline
natively, no emulator: **boot → title → New Game → 3D briefing → enemy tactical intros
→ fly-in → flying the canyon**, rendered through the original voxel/span rasterizer with
shading, textures and HUD. It took **nine lifter fixes** to get here (`fs:` segments,
`ret` stack cleanup, flag-operand snapshots, intra-function jump tables,
`repne scasb`/`strlen`, the **global x87 FPU stack**, the FPU `st(i)` index base, and
the `fstp st(i)` pop semantics) — every one pushed back upstream to the shared
[pcrecomp toolbox](https://github.com/sp00nznet).

| Phase | What | State |
|------:|------|:-----:|
| 0 | Reconnaissance — PE analysis, import map, engine ID | ✅ done |
| 1 | Disassembly — IDA + disasm32 function recovery | ✅ done |
| 2 | Classification — call graph, subsystems, hot path | ✅ done |
| 3 | Lift to C — 1,945 funcs → C, compiles to objects | ✅ done |
| 4 | Shim layer — runtime + 160 import bridges | ✅ done |
| 5 | Build & link — single exe, 0 errors | ✅ done |
| 6 | Bring-up — boots through CRT/WinMain/window into main loop | ✅ done |
| 7 | First pixels — real I/O + assets + DIB render | ✅ done |
| 8 | Playable — input, New Game, briefing, **flying the canyon** | ✅ done |
| 9 | Frontend — SDL2 + Dear ImGui menu, real joystick | ✅ done |

The full diary of how each phase was solved is in **[docs/PROGRESS.md](docs/PROGRESS.md)**.

### Known limitations

- **Audio is stubbed.** The game's mixer runs on a `waveOut` worker thread; wiring it up
  needs thread-local CPU/FPU register state in the recomp, which isn't in place yet.
- **Save/Load State** is exposed in the SDL menu but not wired — recomp save states need
  a re-entrant snapshot of sim memory + CPU/FPU state.
- **Conservative carry-flag model.** The lifter tracks CF in a running variable that
  `cmp`/`sub`/`add` don't publish into. Making it precise deterministically breaks the
  New-Game → briefing transition (the recomp sits in a self-consistent equilibrium), so
  it's left conservative; the one gameplay-affecting case (the cheat reader) is host-shimmed.
  See [docs/PROGRESS.md](docs/PROGRESS.md).
- **Intermittent dt=0 freeze** on roughly 1 run in 5 — the briefing occasionally stalls
  before flight; relaunching clears it.

---

## PS Vita

The same translated code runs natively on the PS Vita, with a GPU renderer, sound, music,
saving, a planet list and setup from a disc image on the memory card. On a Vita 1000 it reaches
about 80 fps in quiet flight and 20-30 in heavy fights. See
[docs/INSTALL_VITA.md](docs/INSTALL_VITA.md) to play and
[docs/PROJECT_HISTORY.md](docs/PROJECT_HISTORY.md) for how it was done.

To build, you need [VitaSDK](https://vitasdk.org), vitaGL and your own `FURY3.EXE`. Lift it as
described under Building, then configure with
`-DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DFURY3_SDL=ON`.

---

## How it works

This repo doesn't ship game assets — bring your own original `FURY3.EXE` and data
files. What lives here is the machinery that turns that binary into source:

```
fury3/
  analysis/        function bounds (ida_funcs.json); your FURY3.EXE goes here (gitignored)
  config/          pe_analysis.json and pipeline configuration
  docs/            PLAN.md (the approach) + PROGRESS.md (the diary) + PHASE*.md
  src/recomp/gen/  (generated) lifted C — produced by run_lift.py
  src/runtime/     hand-written runtime + import shims + SDL frontend
  run_lift.py      driver: IDA bounds → lift32 → src/recomp/gen/
  gen_imports.py   generates the 160 import bridges
  CMakeLists.txt   build (GDI default; -DFURY3_SDL=ON for the SDL/imgui frontend)
```

The recompilation pipeline, all from the shared [pcrecomp toolbox](https://github.com/sp00nznet):

```
FURY3.EXE
   │  pe_analyze.py            ← Phase 0 (done): sections, imports, entry point
   ▼
   │  disasm32.py              ← Phase 1: recursive-descent x86-32 disassembly
   ▼
   │  callgraph + bounds       ← Phase 2: function recovery, classification
   ▼
   │  lift32.py                ← Phase 3: x86 → readable C, one function at a time
   ▼
   │  compat/ (GDI→SDL2 …)     ← Phase 4: replace the 5 DLLs with a thin shim
   ▼
CMake + your C compiler        ← Phase 5+: build, link, run
```

Read the full battle plan in **[docs/PLAN.md](docs/PLAN.md)**.

---

## Building

You provide your own legally-obtained `FURY3.EXE` (this repo holds **no** game code or
assets), at `analysis/FURY3.EXE`. Generate the C, then build.

The lifter is [pcrecomp](https://github.com/sp00nznet/pcrecomp), cloned next to this
folder as `tools`, at the commit in `tools/pcrecomp.commit`, with our patch applied:

```bash
git clone https://github.com/sp00nznet/pcrecomp ../tools
git -C ../tools checkout $(cat tools/pcrecomp.commit)
git -C ../tools apply "$PWD/tools/pcrecomp.patch"
```

```bash
# 1. lift the binary to C  (needs Python 3.11 + capstone; IDA bounds are vendored)
py -3.11 run_lift.py          # -> src/recomp/gen/recomp_*.c

# 2a. plain Win32 / GDI build (default)
cmake -B build -G Ninja
cmake --build build
./build/fury3 analysis/FURY3.EXE --run

# 2b. SDL2 + Dear ImGui frontend (windowed + in-game menu / controls)
cmake -B build -G Ninja -DFURY3_SDL=ON      # or: bash build_sdl.sh
cmake --build build
```

The game also needs its data (the POD archives unpacked via `tools/extract_pod.py`);
point the runtime at them as described in `docs/PLAN.md`.

---

The full phase-by-phase progress diary lives in **[docs/PROGRESS.md](docs/PROGRESS.md)**.

---

## License

Tooling and original code in this repo: MIT. _Fury³_ and all its assets are property
of their respective owners; none are included here. Prebuilt release packages contain
code compiled from the lifted C, which the MIT licence doesn't cover: see `LICENSE`.
This is a preservation and interoperability project.

---

*"everything old is new again." — built with stubbornness by [sp00nznet](https://github.com/sp00nznet)*
