# Fury³ Recompilation — Approach

This is the phase-by-phase battle plan for statically recompiling `FURY3.EXE` (1995)
into native C. It follows the standard pcrecomp pipeline
(**Analyze → Disassemble → Classify → Lift → Shim → Build → Debug → Ship**), tuned to
what we learned about this specific binary in Phase 0.

## What we're working with (Phase 0 findings)

```
FURY3.EXE      645,120 bytes   PE32 / i386 / Windows GUI
ImageBase      0x00400000
EntryPoint     0x00453081
Linker         Microsoft Link 2.55  (1995)
Built          1995-08-14 19:30 UTC

Sections
  .text   0x00401000  383 KB  CODE/EXEC      ← all the game logic + 3D engine
  .bss    0x0005F000  894 KB  uninit data    ← framebuffer, world state, z-buffer
  .rdata  0x0013A000  ~900 B
  .data   0x0013B000   40 KB  init data
  .idata  0x00145000    4 KB  import table
  .rsrc   0x00147000  180 KB  bitmaps/dialogs/menus/strings
  .reloc  0x00173000   33 KB  base relocations  ← great, we get relocation truth

Imports  160 functions / 5 DLLs
  GDI32(20) KERNEL32(61) USER32(60) WINMM(16) comdlg32(3)
```

**Why this is favorable**
- One unpacked `.text`, no SafeDisc/SecuROM/UPX. The entry point sits inside `.text`,
  not a packer stub. Straight recursive-descent disassembly will work.
- `.reloc` is present → we have ground-truth for every absolute address, which makes
  pointer/data-reference recovery far more reliable than guessing.
- Tiny OS surface, and every API in it already has a mapping in pcrecomp's `compat/`
  layer or is trivially implementable. No DirectX bring-up tax.
- Software renderer means **no GPU semantics to reverse** — it's all integer math
  writing bytes into a DIB. Once lifted, it just works on any platform.

**Risks / unknowns to retire early**
- MSVC 2.x CRT is statically linked (no MSVCRT import) → we'll be lifting CRT
  routines too (malloc, memcpy, float helpers). Classify and shim these rather than
  lift them where we can recognize them.
- 8-bit palettized rendering with palette animation — get the palette path right or
  everything looks wrong even when the logic is correct.
- Fixed-point / x87 FPU math in the 3D transform. The lifter has an x87 decoder;
  verify FPU stack discipline on the hot transform functions.
- Resource-driven UI (`.rsrc` dialogs/menus). We may stub the Win95 config dialogs
  and drive settings from the `.INI` directly rather than recompiling DialogBox logic.

---

## Phase 1 — Disassembly

Goal: a complete, correct instruction-level map of `.text`.

```bash
python tools/disasm/disasm32.py analysis/FURY3.EXE \
    --pe-json config/pe_analysis.json \
    --output analysis/functions.json
```

- Recursive descent from the entry point + every address named in `.reloc` + exported
  callbacks (WndProc, timer/audio callbacks registered with USER32/WINMM).
- Cross-check coverage against `.text` size; flag gaps as either data-in-code
  (jump tables, embedded palettes) or undiscovered functions.
- **Optional accelerator:** if Ghidra/IDA is available, run `tools/ghidra/DumpBounds.java`
  to get verified function boundaries and feed them back in. This dramatically improves
  function recovery on hand-optimized 1995 code.

Exit criteria: ≥95% of `.text` classified as code vs data, jump tables resolved.

## Phase 2 — Function recovery & classification

```bash
python tools/disasm/callgraph.py analysis/FURY3.EXE --bounds analysis/bounds.csv --leaves
python tools/classify/combined_classify.py analysis/functions.json --output analysis/classes.json
```

- Build the call graph; identify leaves (pure math — prime lift candidates) vs
  OS-touching functions (need shims) vs CRT (recognize & replace).
- Classify into buckets: **engine/render**, **physics/math**, **AI/mission**,
  **input/UI**, **audio**, **file/asset I/O**, **CRT**, **Win32 glue**.
- Name the hot functions (`callgraph --hot 25`). The 3D inner loops will dominate;
  those get human attention first.

Exit criteria: every function tagged; the ~20 hottest functions identified by name.

## Phase 3 — Lift to C

```bash
python tools/lift/lift32.py --functions analysis/functions.json \
    --pe-json config/pe_analysis.json --output src/recomp/gen/
```

- Lift function-by-function to C against the recomp32 runtime (global register model,
  flat memory, dispatch table). Output is compilable but ugly — that's expected.
- Order of attack: leaves first (math/transform), then their callers, working up the
  call graph toward `WinMain`. Keep the build green at every step.
- Hand-promote the hot transform/raster functions from literal lifts to clean C once
  their behavior is pinned by tests.

Exit criteria: all classified code lifted; `src/recomp/gen/` compiles as objects.

## Phase 4 — Shim layer (the only real "porting" work)

Replace the 5 DLLs with a thin SDL2-backed compat layer. This is small and bounded:

| DLL | What the game uses it for | Shim target |
|-----|---------------------------|-------------|
| **GDI32** | `CreateDIBSection`, `StretchBlt`, `SetDIBColorTable`, `CreatePalette`, `BitBlt`, `SelectObject`, `CreateCompatibleDC` | The DIB *is* our framebuffer. `StretchBlt` → upload `SDL_Texture` + `SDL_RenderCopy`. Palette → `SDL_SetPaletteColors` / shader LUT. |
| **USER32** | window create, `GetMessage`/`DispatchMessage`, keyboard & mouse, timers | `SDL_CreateWindow` + an `SDL_PollEvent` pump that synthesizes the WM_* the WndProc expects. |
| **KERNEL32** | file I/O, `GlobalAlloc`/`GlobalLock`, `GetPrivateProfile*` (the .INI), `FindResource` | libc file I/O; resources read from a packed copy of `.rsrc`; INI via a tiny parser. |
| **WINMM** | `waveOutOpen/Write/Reset/...` PCM streaming | `SDL_AudioStream` fed by the same PCM buffers. |
| **comdlg32** | `GetOpenFileNameA` ×3 | native file dialog or a fixed path; lowest priority. |

Most of these already exist in `runtime/compat/` from prior projects (Win32→SDL2
mapping); vendor them in and fill the Fury³-specific gaps.

Exit criteria: every imported symbol resolves to a real implementation or a logged stub.

## Phase 5 — Build & link

- Drop in `templates/CMakeLists.txt.template`, wire `src/recomp/gen/` + `runtime/` + shims.
- `cmake -B build && cmake --build build` until it **links**. Resolve duplicate CRT
  symbols, calling-convention mismatches, and missing data relocations here.

Exit criteria: a `fury3` binary that links and starts (even if it immediately faults).

## Phase 6 — Bring-up

- Get past `WinMain` init: window opens, palette loads, the message loop ticks without
  crashing. Use the recomp32 trace facility to walk the first thousand calls and fix
  the inevitable early data-reference and stack bugs.

Exit criteria: stable main loop, no fault for 60 seconds at the menu.

## Phase 7 — First pixels

- Make the renderer write a real frame into the DIB and see it on screen. This is the
  "it's alive" moment — even a garbled frame proves the lift of the raster path is
  structurally correct. Then fix palette, aspect, and the StretchBlt scaling.

Exit criteria: a recognizable rendered frame from the game.

## Phase 8 — Playable

- Wire input fully, get the flight model and collision ticking at the right rate, audio
  streaming, and fly the first canyon end-to-end. Iterate on correctness against the
  original running side-by-side.

Exit criteria: the first level is playable start to finish.

---

## Working rhythm

- **Keep the build green.** Every merge compiles. Broken-but-isolated lifts get stubbed,
  not committed red.
- **Verify against the original.** Where behavior is ambiguous, run real Fury³ (in a VM
  with Win32s) and compare — register traces for hot functions, screenshots for the
  renderer, captured PCM for audio.
- **Promote hot paths by hand.** The literal lift is the scaffold; the inner loops get
  rewritten into clean, fast, readable C once tests pin their behavior.
- **Log every cap.** If we stub a dialog or skip a code path, it goes in the progress
  log in the README — no silent gaps.

## Definition of done

`FURY3.EXE` is gone. In its place: a CMake project that compiles from C on Windows,
Linux, and ARM, opens a window with no Win32s and no emulation, and lets you fly the
fury. Every line traceable back to the 1995 binary it came from.
