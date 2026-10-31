# Phase 7 — Real I/O & Assets (in progress)

**Status: 🟡 in progress (2026-06-13). The game runs ~2.7M function calls into its
own logic with real data loaded.** It boots, loads its actual POD game assets, parses
data files, and runs deep into model/asset loading before an asset-lookup edge case.

## What got built

With bring-up complete (Phase 6), the game needed real file I/O and its data. We built
a complete host-backed I/O layer and gave it the real assets.

### A real file + stdio layer (`recomp_runtime.c` + `shims_impl.c`)

The game does its file access through the **MSVC CRT stdio family**, and the lifted
`_iob` FILE table isn't viable, so we host-shim the whole family onto real files:

- **`CreateFileA`/`ReadFile`/`WriteFile`/`SetFilePointer`/`CloseHandle`/`GetFileType`**
  — Win32-backed, with a small handle table.
- **`fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell`/`fgets`/`_filbuf`/`fprintf`/`fflush`**
  — host-shimmed. `fopen` returns a *real, mapped* MSVC-style FILE struct (with
  `_cnt=0` so `getc` falls through to `_filbuf`, and our file-slot index in `_file`),
  because the game dereferences the FILE pointer directly.
- **`_stat`** — fills the MSVC `_stat` struct (st_mode/st_size) from host file info.
- **`GetPrivateProfile*`/`WritePrivateProfileStringA`** — real `FURY3.INI` via Win32.
- A **null-safe formatter** shared by `sprintf`/`printf`/`fprintf` (prints `(null)`
  for a null `%s`, matching MSVC) — the game floods debug logs, and the lifted
  `__output` dereferenced null args.

### Path translation + basename index

Game paths (`.\system\fury3.ini`, `C:\FURY3\...`, bare `mapback.raw`) are translated
to live under `gamedata/`. The game also opens many assets by **bare name**
(`laser.bin` is really `MODELS/LASER.BIN`), so we build a one-time **basename index**
of the game tree and resolve bare names against it when the literal path is missing.

### The game's data: POD archives

Fury3's data lives in **POD archives** (`startup.pod` 4 MB, `fury3.pod` 44 MB —
Terminal Reality's format) shipped on the CD. `tools/extract_pod.py` unpacks them
(`u32 count`, 80-byte comment, then `{name[32], size, offset}` entries) — **3,581
asset files**: ART, LEVELS, MODELS, MUSIC, SOUND, FOG. We also author `pod.ini` (the
`[Game]` POD manifest the game reads).

## How far it runs

```
WinMain → window → frame loop → load VGA palette (ART/VGA.ACT) → parse data files
(fscanf via _filbuf) → load models/assets → ... ~2,676,410 function calls ...
```

The game loads its real palette, parses its config and data files, and runs deep into
asset loading — millions of instructions of *actual game logic*, all reading real
data off disk through the shim layer. It currently exits(1) after the intro/init when
it can't resolve one asset (`laser.bin`).

## The render path is built — and a window opens

Implemented the full display layer (`recomp_runtime.c`), shimmed in `shims_impl.c`:

- **A real Win32 window.** `CreateWindowExA` creates an actual window (2× the 320×200
  surface) with our own `WndProc` (handles close/escape). It appears on screen:
  `[gfx] window created (320x200)`.
- **A real DIB the game renders into.** `CreateDIBSection` parses the game's
  `BITMAPINFOHEADER`, allocates a real 8-bpp pixel buffer **in simulated memory**, and
  hands its VA back through `ppvBits` — so the game's rasteriser writes into memory we
  own and can present.
- **Palette.** `SetDIBColorTable` captures the 256 RGBQUADs.
- **Blit.** `StretchBlt`/`BitBlt` → `StretchDIBits` the 8-bpp buffer to the window,
  scaled, with the palette.
- **Message pump.** `PeekMessageA`/`GetMessageA` pump the real window (so it's live and
  closable) and return "no message" so the game keeps rendering frames; a window-close
  delivers `WM_QUIT` so the game loop exits cleanly.

Also host-shimmed the game's own file-length helper (`sub_0042A400`) onto real file
sizes, since its POD-directory lookup isn't wired up — that unblocked `laser.bin` and
the asset load past it.

## A host heap — and the DIB surfaces get created

The lifted MSVC small-block heap proved fragile: deep in 3D-object init, `_free`
dereferenced a corrupt block header. Rather than debug the lifted heap, we
**host-shimmed `malloc`/`free`/`realloc`/`calloc`** onto a simple free-list allocator
(16-byte block headers, first-fit reuse). That got the game past the crash — and it
then **creates its `CreateDIBSection` surfaces**:

```
[gfx] window created (320x200)
[gfx] CreateDIBSection 320x200 8bpp -> buf 0x10669DA0
[gfx] CreateDIBSection 320x200 8bpp -> buf 0x10696770   (triple-buffered)
[gfx] CreateDIBSection 320x200 8bpp -> buf 0x106B5C20
```

So the **render path fires**: a window is up, and the game has real framebuffers (in
simulated memory) to draw into. The runtime auto-presents whichever surface has the
most content on every message pump (`StretchDIBits` to the window).

## Into the interactive state — and a WndProc bridge

The game reaches its **interactive menu/idle state**: it prints `Texture cache size :
512`, tries to play its intro movie (`system/story/fury.avi`, absent), shows a dialog
(`DialogBoxParamA`), then settles into a steady **idle loop polling input**
(`0x40171C`) at ~16 fps.

It never calls GDI `StretchBlt`/`BitBlt` itself — because the game does its blit in
its **own `WndProc` (`WM_PAINT`)**, and we had created the window with *our* WndProc.
Fixed: `RegisterClassA` now captures the game's `WndProc` VA, `InvalidateRect`
triggers a real `WM_PAINT`, and our window proc **bridges messages back into the
game's lifted `WndProc`** (via `recomp_call_va`, saving/restoring callee-saved regs so
the re-entrancy can't corrupt the game across its `PeekMessage`). The runtime also
auto-presents the active DIB each pump.

## Where it stands now

The game **runs stably in its interactive idle loop** with the window up, its DIB
surfaces created, the palette captured, and the WndProc bridged — but the framebuffers
stay empty (no pixels drawn yet). The draw is gated deeper in the game's display/state
logic: the menu/3D render hasn't been triggered (it idles past the missing intro AVI,
likely waiting on input or a display path that routes through `DISPDIB.DLL`/`WING32.DLL`
— both reported absent — rather than the GDI path we present).

## Where the input gate is

The idle loop polls `[0x541F14]` (a "key available" flag) via `sub_00446E90`; the
game's `WndProc` sets it (`sub_00446EB0`) on input and the tick-sync clears it each
frame. So the game waits for a keypress to advance. The WndProc bridge confirmed it
**delivers the real window's messages to the game** (`WM_NCCREATE`/`WM_CREATE`/…), so
the game's window-init runs (it now allocates 5 DIB surfaces vs 3). But forcing the
flag / injecting keys didn't make it draw — the render gate is deeper than input
(suspects: the failed copy-protection `fury3pid.dll`, or the intro/MCI state).

## FIRST PIXELS — it was an FPU stack-indexing bug

The "uninitialized read" below turned out to be **the 8th lifter bug, and it gated the
render too**. FPU register-relative operands (`fld`/`fstp`/`fsub`/`fxch st(i)` and FPU
sources) were lifted as `_st[reg_id - 224]`, assuming an old capstone numbering — but
this capstone's `X86_REG_ST0` is **114**, so `st(0)` lifted to `_st[-110]`. The `_st[]`
FPU stack is an 8-element array, so every register-relative FPU op was reading/writing
~110 doubles out of bounds, **scribbling over adjacent globals** (`g_ecx`, `g_fp_top`,
…). That's what crashed the un-instrumented build (a codegen change could move what got
clobbered — the "heisenbug") *and* corrupted the renderer's math so nothing drew.

Fixed in the lifter (`reg_id - X86_REG_ST0`). Result: the un-instrumented build is
**stable**, and the game **renders its screen into the DIB** (a non-zero framebuffer),
which the runtime presents to the window via `StretchDIBits`. The WndProc bridge was
re-enabled (its `WM_PAINT` blit + input handling) and is stable too.

**The recompiled Fury3 puts pixels on screen.** Next (Phase 8): drive input so the
game advances past the title, wire `waveOut` audio, and fly the first canyon.

## (Historical) the known issue, before it was diagnosed

The fully-instrumented build (`-DRECOMP_TRACE`, a hook before every lifted function)
runs **stably for 50 s+** in the idle loop. The un-instrumented build hits an access
violation within seconds — at both `-O0` and `-O1`. Since the only difference is the
per-function entry hook (which writes a global ring buffer and so touches the stack
before each function body), this points to a **lifted function reading an
uninitialized local/stack slot**: the hook happens to leave a benign value there. The
traced build is the current known-good configuration; finding and fixing that
uninitialized read is a prerequisite for a clean release build.

## Current frontier

1. **Fix the latent uninitialized read** so the un-instrumented build is stable.
2. **Trigger the first draw** — the game idles without rendering; determine where it
   draws (menu `WM_PAINT`, or the 3D view once a game starts) and what gates it
   (copy-protection / intro state), then re-enable the WndProc bridge with a safe
   re-entrancy model so the game's `WM_PAINT` blit reaches the screen.
3. The game's POD virtual-FS (worked around via loose-disk extraction + a host
   file-length shim).

Everything else is in place: real I/O, 3,581 real assets, a heap, a window, DIB
framebuffers, palette, presentation, a captured WndProc, and message bridging. The
last steps are the uninitialized-read fix and triggering the render.

## Artifacts

- `tools/extract_pod.py` — POD1 archive extractor (committed)
- `gamedata/` — extracted assets + PODs (gitignored; bring your own Fury3 data)
- `src/runtime/recomp_runtime.c` — host file/stdio/INI/stat layer + basename index
- `src/runtime/shims_impl.c` — the stdio + I/O shims

No pcrecomp/lifter changes this phase — it's all project-side runtime/shims.
