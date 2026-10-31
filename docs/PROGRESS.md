# Fury³ recompilation — progress log

Newest first. This is the running diary of the recompilation. For the high-level
state and how to build, see the [README](../README.md).

### 2026-06-14 — Phase 8: lifter CF investigation closed (kept conservative)
Investigated the long-standing `cmp → CF` lifter imprecision: the carry flag is
tracked in a running `_cf` variable that shifts/`stc`/`rcr` set, but `cmp`/`sub`/`add`
do **not** publish into it, so a `cmp X,Y; sbb r,r` mask idiom reads a stale carry.
Tried four fixes — publishing CF globally at every `cmp`/`sub`, an adjacent-only
synthesis (only when the flag-setter is the immediately-preceding instruction),
a `cmp`-only variant, and the absolute narrowest `sbb r,r`-only variant. **Every
variant deterministically broke Fury³'s New-Game → briefing transition** (the game
gets stuck in the menu loop, `rmode` never advances to 2) while the baseline reaches
flight reliably (flight samples ~76–82 per run). The recompiled binary has settled
into a **self-consistent equilibrium** where the CF imprecision is compensated by
behaviour downstream on that path; making CF *correct* in isolation breaks it.
Conclusion: keep the conservative `_cf` model; the one gameplay-affecting case (the
cheat-code reader `sub_43BFB0`) stays handled by a targeted host shim. A real fix
needs a per-site differential trace of the briefing transition, not a global flip.
Documented in `lift32.py`'s `sbb` handler.

### 2026-06-13 — Phase 8: modern frontend — SDL2 + Dear ImGui menu 🎛️
Pulled the SDL2 + Dear ImGui frontend pattern from the sibling pcrecomp projects
(snesrecomp/apple2recomp/lynxrecomp) into Fury³. The optional `-DFURY3_SDL=ON` build
wraps the game in a resizable SDL window that streams the 8bpp framebuffer (+ palette)
into an `SDL_Texture`, with an in-game menu bar over it: **File** (New Game ·
Save/Load State · settings), **Video** (scale · nearest/linear · v-sync · fullscreen),
**Audio** (volume · mute), **Controls**, all persisted to `fury3_config.ini`, toggled
with **F1**. The Controls panel binds every flight action to a keyboard key *and* an
Xbox-style gamepad button.

### 2026-06-13 — Phase 8: real joystick support 🕹️
Un-stubbed the WINMM joystick path for the flight-sim crowd. `joyGetNumDevs`,
`joyGetPos`, and `joyGetDevCapsA` now forward to the host's real joystick (with
`joyGetNumDevs` probing for *connected* devices, not just supported ports, so the
game doesn't stall on a phantom stick). A physical flight stick drives the analog
controls directly.

### 2026-06-13 — Phase 8: flying the canyon ✈️
The full pipeline runs natively: **boot → title → New Game → 3D briefing → enemy
tactical intros → fly-in → flying the canyon**, all rendered through the original
voxel/span rasterizer with shading, textures and HUD. Fixed the per-enemy
tactical-briefing intro crash, the dt=0 briefing freeze (the throttled frame timer
shared a `GetTickCount` tick → switched `timeGetTime` to a hi-res source), and a
stray injected key that opened a modal pump during the briefing.

### 2026-06-13 — Phase 8: SOLVED — the title renders in colour (9th lifter bug) ✅
Cracked the colour collapse by comparing our lift of the palette-ramp builder
(`sub_00443040`) against **IDA Hex-Rays' decompilation of the original**. The
decompiler showed a clean accumulator loop (`byte[i]=(int)v5; v5+=step`); ours wasn't
accumulating because the x87 stack desynced. The bug: **`fstp st(i)` was lifted as
`_st[i] = fp_pop()`** — but x86 copies `ST(0)→ST(i)` *then* pops (value lands at
`st(i-1)`), and `fstp st(0)` must just discard. Fixed in the lifter to
`{ _st[i] = _st[0]; fp_pop(); }` and pushed to pcrecomp. **The recompiled Fury³ now
renders its 166-colour "Microsoft FURY³" title screen through its own palette — no
hacks.** A genuine FPU-stack fix that likely also helps the 3D-transform/shading paths.

### 2026-06-13 — Phase 8: the title screen renders in full colour 🎨
The "frozen 2-colour screen" turned out to be the **Fury³ title screen** — loaded
correctly all along, then wrecked by one bad lookup table.
- Proved it: the displayed buffer is a perfect per-pixel remap of `ART/TITLE.RAW`
  (0 conflicts over its 166 colours), and the host `fread` reads all 166 colours in.
- The generic image loader remaps every pixel through a 256-byte palette-translation
  table at `0x47C770`, which is built wrong (`[0..31]→10, [32..255]→0`) by an FPU-heavy
  builder (`sub_00444D10`) even though its input palette (vga.act) is correct.
- **Forcing that table to identity yields the pixel-perfect, full-colour "Microsoft
  FURY³" title screen.** So the whole front-end pipeline is correct — load, remap,
  snapshot, blit, present — and only this one table is wrong.
- Also fixed `DispatchMessageA` to route to the WndProc, so the game's flag-polling
  input loops work (both input styles now functional).

### 2026-06-13 — Phase 8: input works, New Game starts 🎮
The title wasn't frozen on a bug — it was waiting for input we never delivered.
- **The game reads keys from its own `PeekMessage` loop**, not its `WndProc` (which
  forwards `WM_KEYDOWN` to `DefWindowProc`). Our shim consumed every message and
  returned "no message," so the loop saw nothing. Now `PeekMessage`/`GetMessage` hand
  real input to the game's `MSG`, while we dispatch only window-upkeep messages.
- **F2 = New Game is an accelerator**, not a keystroke. Implemented
  `TranslateAcceleratorA`/`LoadAcceleratorsA`: F2 → `WM_COMMAND 32784` into the
  `WndProc`, which the game *does* handle. **New Game now runs.**
- **Host `fscanf`**: the lifted MSVC scanf core bypassed our file layer, so manifest
  parsing failed. A compact host reader fixed it — the game parses `mission.ini` and
  its `.LVL` files (count + component filenames) and builds its campaign table.
- **A function IDA dropped** (reached only via a jmp-thunk chain → `ITAIL: unresolved`)
  is now recovered — the disasm fix went upstream to pcrecomp.

### 2026-06-13 — Phase 7: FIRST PIXELS 🎉
Fixed the bug behind both the crash and the blank screen — the game now renders.
Details in [PHASE7.md](PHASE7.md).

- **8th lifter fix (FPU stack indexing).** `fld/fstp/fsub/fxch st(i)` and FPU source
  operands were lifted as `_st[reg_id - 224]`, but this capstone's `X86_REG_ST0` is
  **114** — so `st(0)` became `_st[-110]`, an out-of-bounds write/read on the 8-element
  `_st[]` array that **corrupted adjacent globals on every FPU op**. That was the
  "uninitialized read" heisenbug (it scribbled over `g_ecx`/`g_fp_top`/…) *and* why
  nothing drew. Fixed to `reg_id - X86_REG_ST0`. Pushed to pcrecomp.
- Result: the un-instrumented build is **stable**, and the game **renders its screen
  into the DIB** (non-zero framebuffer) which we present to the window via
  `StretchDIBits`. Re-enabled the WndProc bridge (its `WM_PAINT` blit + input) — stable.
- **The recompiled Fury3 puts pixels on screen.**

### 2026-06-13 — Phase 7: input gate found; a heisenbug surfaced
Pinned the input gate and bridged window messages; uncovered a latent lifter bug.
Details in [PHASE7.md](PHASE7.md).

- The idle loop waits on a "key available" flag (`[0x541F14]`, set by the game's
  WndProc on input). Confirmed the **WndProc bridge delivers the real window's
  messages to the game** (`WM_CREATE`/`WM_SHOWWINDOW`/…) — its window-init now runs
  (5 DIB surfaces). But forcing input didn't make it draw: the render gate is deeper
  (suspect the failed copy-protection `fury3pid.dll` or the intro/MCI state).
- **Known issue:** the instrumented build (entry hook before every lifted function)
  runs stably 50s+; the un-instrumented build access-violates within seconds (at -O0
  and -O1). The hook touches the stack before each function body, so this points to a
  **lifted function reading an uninitialized local** that the hook masks. The traced
  build is the current known-good config; this is the next thing to fix.

### 2026-06-13 — Phase 7: into the interactive menu/idle state
Traced the game into its interactive state and bridged its window proc. Details in
[PHASE7.md](PHASE7.md).

- The game reaches its **interactive menu/idle loop**: `Texture cache size : 512`,
  tries its intro movie (`story/fury.avi`, absent), then idles polling input at ~16fps.
- It blits in its **own `WndProc` (`WM_PAINT`)**, which never ran (our window used our
  proc). Fixed: `RegisterClassA` captures the game's WndProc, `InvalidateRect`
  triggers real `WM_PAINT`, and our proc **bridges messages into the game's lifted
  WndProc** (regs saved/restored for re-entrancy).
- Runs stably in the idle loop — window up, DIBs created, palette + WndProc bridged —
  but framebuffers are still empty: the draw is gated deeper (menu/3D render not yet
  triggered; the game's preferred present path is DISPDIB/WinG, reported absent).

### 2026-06-13 — Phase 7: a host heap, and the DIB surfaces appear
Replaced the lifted heap and got the game to create its render surfaces. Details in
[PHASE7.md](PHASE7.md).

- The lifted MSVC small-block heap was corrupting block headers (`_free` faulted deep
  in 3D-object init). **Host-shimmed `malloc`/`free`/`realloc`/`calloc`** onto a
  free-list allocator — past the crash.
- The game then **creates its `CreateDIBSection` surfaces** (320×200 8bpp, triple-
  buffered) — the render path fires. The runtime auto-presents the active surface
  via `StretchDIBits` each message pump.
- It now **runs its frame loop at ~16 fps** (480+ frames; `PeekMessage` + a
  `get_time()>>16` tick-sync). The framebuffers are still empty — the game loops
  without drawing yet (rendering gated behind a state/subsystem it hasn't entered).

### 2026-06-13 — Phase 7: the render path + a window on screen
Built the display layer and got a real window to open. Details in
[PHASE7.md](PHASE7.md).

- **A real Win32 window opens** (`CreateWindowExA` → real window + WndProc).
- `CreateDIBSection` hands the game a real 8-bpp pixel buffer (via `ppvBits`) in
  simulated memory; `SetDIBColorTable` captures the palette; `StretchBlt`/`BitBlt`
  → `StretchDIBits` present it to the window; `PeekMessage`/`GetMessage` pump it.
- Host-shimmed the game's file-length helper (its POD-dir lookup wasn't wired) to
  unblock `laser.bin` and the asset load past it.
- The game now runs 10k+ calls into 3D model/object processing (the FPU math core)
  toward the first frame; faults on a null-struct deref in that init — the last gap
  before the DIB blit fires.

### 2026-06-13 — Phase 7: real data, 2.7M calls deep
Gave the running game real file I/O and its actual assets. Full writeup in
[PHASE7.md](PHASE7.md).

- Host-shimmed the **whole CRT stdio family** (fopen/fread/fseek/fgets/fprintf/…)
  onto real files — `fopen` returns a real mapped FILE struct since the game
  dereferences it. Plus `CreateFile`/`ReadFile`, `_stat`, real `GetPrivateProfile*`,
  and a null-safe `sprintf`/`printf`/`fprintf`.
- Wrote `tools/extract_pod.py` and unpacked the game's **POD archives** (startup.pod
  4 MB + fury3.pod 44 MB) → **3,581 real asset files** (ART/LEVELS/MODELS/MUSIC/…).
- Added a **basename index** so bare-name asset opens (`laser.bin` → `MODELS/LASER.BIN`)
  resolve, and a `pod.ini` manifest.
- Result: the game **boots, loads its real VGA palette + data, parses config/data
  files, and runs ~2,676,410 function calls** into actual model/asset loading.

### 2026-06-13 — Phase 6: it boots into the main loop
Two more lifter fixes took it from "stuck before the window" to running its whole
startup. Details in [PHASE6.md](PHASE6.md).

- **`repne scasb` / `strlen` fix** (lifter): the `strlen` idiom was a pre-test loop,
  so `strlen("")` returned `0xFFFFFFFF` — WinMain's command-line copy ran 300 KB off
  the image. Fixed to real do-while semantics. This *was* the cmdline overrun.
- **Global x87 FPU stack** (lifter): `_st`/`_fp_top` were per-function locals, but
  `__ftol` receives its arg on the FPU stack from the caller — so the frame timer
  `get_time = (int)(timeGetTime()*1179.648)` was always 0 and the main loop spun
  forever. Made the FPU stack global. Both pushed to pcrecomp.
- Implemented a real `timeGetTime` (host ms) + a `MessageBoxA` that prints the game's
  own diagnostics. Surfaced and fixed: "Cannot load accel!", "Unable to write .INF",
  "Unable to create sound thread" (stubs now report success).
- The game now **boots end-to-end**: RegisterClass → CreateWindowExA → ShowWindow →
  frame loop → config → display setup (GetDeviceCaps/GetDC) → PeekMessage loop →
  CreateThread — 26 OS APIs deep, into the main loop. **That's seven lifter bugs fixed.**

### 2026-06-12 — Phase 6: WinMain reached
Pushed from CRT-init all the way into the game's own entry point. Details in
[PHASE6.md](PHASE6.md).

- Fixed **two more fundamental lifter bugs** (both general, beyond Fury3):
  4. **Conditions read clobbered operands** — a `jcc` re-evaluated the flag operands
     at jump time, so `test eax,eax; mov eax,0; jne` tested the *zeroed* eax. Now the
     flag operands are snapshotted into temps at the flag-setter. (Pushed to pcrecomp.)
  5. **Intra-function jump tables** (switch statements) — an indirect `jmp [table]`
     lifted to a global tail-call the dispatch couldn't resolve to a label *inside*
     the function. Now functions with indirect jumps get a per-function local-label
     `switch` dispatch; jump tables resolve.
- Added a **function-entry tracer** (`RECOMP_ENTER`, `-DRECOMP_TRACE`) — our backtrace,
  since the local gdb is broken. It pinned every fault to an exact function.
- Result: **`WinMain` (`sub_0040BA90`) is entered** and runs real init (current dir,
  module path, building its help-file path).

### 2026-06-12 — Phase 6: host-shimmed sprintf; into WinMain's own code
- Added a **host-shim mechanism** (`run_lift.py` `HOST_SHIM` set): listed CRT funcs
  aren't lifted — the dispatch points at a hand-written body in `shims_impl.c`.
- First shim: **`_sprintf`** — a mini-`printf` reading varargs from the simulated
  stack and formatting via the host CRT. Sails past the `__output` overrun.
- WinMain now runs further and overruns in its **own command-line copy** —
  `lpCmdLine` is a non-NUL-terminated heap pointer (the CRT's `_setargv`/cmdline
  path, built on our stubbed command line). That's the next target before the window.

### 2026-06-12 — Phase 6: the C runtime runs
Debugged the recompiled entry point from its first faulting instruction all the way
through MSVC CRT startup. Full writeup in [PHASE6.md](PHASE6.md).

- Fixed **two fundamental lifter bugs** (both pushed to pcrecomp):
  1. `fs:`/`gs:` segment overrides were dropped → `fs:[0]` lifted to `MEM32(0)` (null).
     Now routed through a runtime `FS_BASE` pointing at a simulated TIB.
  2. `ret` never popped the return address → ESP drifted 4 bytes per call until the
     dummy sentinel got read as an argument (caught a `VirtualAlloc(0xDEAD1000)`!).
- Built a CRT-bootstrap shim layer: a 128 MB simulated heap (page-aligned bump
  allocator behind `VirtualAlloc`/`GlobalAlloc`) + ~22 hand-written shims (command
  line, environment, std handles, module path, `ExitProcess`).
- Result: the binary now runs the **entire CRT startup** — heap init, `_ioinit`,
  argv/env, `_initterm` initializer tables — and exits cleanly via `ExitProcess(0)`.

### 2026-06-12 — Phase 4: it links, and the runtime boots
Wrapped the lifted code in a runtime and shimmed every import. Full writeup in
[PHASE4.md](PHASE4.md).

- Built the runtime: register model, a **PE loader that maps the original image at
  0x00400000** (host linked high at 0x70000000 so the VA range is free → 1:1 mapping,
  `MEM32(va)` reads real bytes), a 1 MB simulated stack, and dispatch + VEH.
- Generated **160 import bridges** (GDI/KERNEL/USER/WINMM/comdlg32) — stack-correct
  stdcall stubs that keep the simulated stack balanced. `gen_imports.py` carries each
  API's arg count.
- The IAT self-patch trick: write each slot's own VA into it so both `RECOMP_ICALL`
  (direct) and `RECOMP_ITAIL(MEM32(slot))` (thunked) resolve to the same bridge.
- **It links** — runtime + 160 bridges + 1,932 functions → one 1.9 MB exe, 0 errors.

### 2026-06-12 — Phase 3: 1,932 functions become 124K lines of compiling C
Lifted the whole binary. Full writeup in [PHASE3.md](PHASE3.md).

- **1,932 functions → 124,518 lines of C, 0 lift errors**, ~1 second. Driver
  (`run_lift.py`) seeds from IDA's exact `[start,end)` bounds, so no decode bleeds
  across data-in-code.
- **All 6 translation units compile to objects, 0 errors** (5.5 MB of object code).
- Generated a **1,932-entry dispatch table** so indirect `call [ptr]` sites (incl.
  the 100 function-pointer-dispatched funcs from Phase 2) resolve at runtime.

### 2026-06-12 — Phase 2: mapped, and the shim boundary is tiny
Built the call graph + import-usage + FPU-density map from IDA and classified all
739 real functions. Full writeup in [PHASE2.md](PHASE2.md).

- **Only 58 functions touch the OS at all** — 12 render, 7 audio, 2 input, 28 window,
  9 io. That 58-function ring is the *entire* porting surface; the other 681 are
  self-contained computation.
- **26 functions are the FPU-dense 3D-math core** (the `0x42Axxx`/`0x42Exxx` cluster
  looks like one vector/matrix module — `0x42E250` is 87% FPU ops).
- Caught an **MSVC `/INCREMENTAL` build**: 721 jump-thunks were hiding the real call
  graph; collapsing them revealed the true fan-in.

### 2026-06-12 — Phase 1: disassembled, and it's small
Ran two passes over `.text`: pcrecomp's `disasm32` (portable baseline) and IDA Pro 9.1
headless (authoritative). Full writeup in [PHASE1.md](PHASE1.md).

- IDA recovered **1,932 functions** / 86,782 instruction heads. `.text` is 383 KB but
  only ~297 KB is code — the rest is data-in-code (jump tables, palettes, string
  tables) split across 1,086 ranges. This is why we trust IDA over a linear sweep.
- The real workload is **739 hand-written functions (~234 KB)**. The other ~1,200 are
  FLIRT-recognised CRT (313 — we *shim* these) and thunks (881 — trivial).
- **No function is larger than 3.2 KB.** The engine core lives in ~272 medium/large
  functions; the rest lift almost mechanically. This is a small, friendly target.

### 2026-06-11 — Phase 0: it's Fury³
Surveyed ~1,080 Windows 3.x/9x titles for the next recomp target. Shortlisted five,
picked Fury³. Ran `pe_analyze` on `FURY3.EXE`:

- Single 645 KB PE32, i386, one un-packed `.text` (383 KB of code), no DRM.
- **160 imports, 5 DLLs**: GDI32 (20), KERNEL32 (61), USER32 (60), WINMM (16), comdlg32 (3).
- Renderer is a software rasterizer → 8-bit DIB → `StretchBlt`. No DirectX anywhere.
- Verdict: about the cleanest OS surface a 1995 commercial 3D game could possibly have.
