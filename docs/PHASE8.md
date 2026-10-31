# Phase 8 — Into the front-end: input, accelerators, and the mission loader

**Status: 🟢 the game boots, renders, takes input, and starts a New Game — it loads
its campaign manifest and opens the first level file.** This phase closed the input
gap that left the title screen frozen and drove the game through its menu into the
mission-loading path.

## Where Phase 7 left it

After "first pixels," the game ran stably in its attract/menu loop with a window up
and a frame on screen, but it never advanced: injected keystrokes reached the game's
`WndProc` yet nothing happened. The render was correct (the steady 2-colour frame is a
real 2-colour front-end screen, and the per-frame `timeGetTime` tick-wait `sub_00446B60`
works as intended) — the game simply wasn't *acting* on input.

## A missing function IDA dropped (→ pcrecomp)

First, a real recompiler bug surfaced as `ITAIL: unresolved VA 0x004017C6` during the
front-end. `0x4017C6` is a `jmp`-thunk to `0x44FBB0` (a small flag-check helper); both
were reached **only** through a jmp-thunk chain — no direct `CALL`, no standard
prologue — so IDA's catalog (and a plain call-target scan) missed them.

- **pcrecomp `disasm32.py`**: `find_functions` now iterates to a fixpoint, following
  unconditional-jmp/call targets that land on uncovered code. (committed upstream)
- **`run_lift.py`**: `_recover_missing_funcs` does the same against IDA's catalog as the
  coverage base, recovering 10 functions IDA missed (with exact `[start,end)` bounds)
  and unioning them into the lift. Result: `ITAIL: unresolved` is gone.

## The input gap: the game reads keys from *its own* PeekMessage loop

The game's `WndProc` (`0x40AB30`) forwards `WM_KEYDOWN` straight to `DefWindowProc` — it
does **not** read keys there. It reads them from the **result of its own
`PeekMessage` loop**. Our `PeekMessageA` shim was calling an internal pump that
consumed and dispatched every message, then returned "no message" — so the game's loop
never saw a keystroke.

Fixed (`recomp_runtime.c` + `shims_impl.c`): `PeekMessageA`/`GetMessageA` now hand real
input messages (`WM_KEY*`/`WM_CHAR`/mouse) to the game's `MSG` (remapping the host
`HWND` to the game's fake handle), while we still dispatch window-upkeep messages
(paint/close) ourselves. A synthetic **F2** is posted once so the game receives input
with no real keyboard focus.

## F2 = New Game is an *accelerator*, not a keystroke

The front-end keys are an `RT_ACCELERATOR` table (id 128): **F2 → command 32784 (New
Game)**, F1 → Help, etc. The game's loop calls `TranslateAcceleratorA`, which converts
the keydown into a `WM_COMMAND` the `WndProc` *does* handle (it dispatches on `0x111`).
Our stub returned 0, so F2 did nothing.

Implemented (`shims_impl.c`, now in the `MANUAL` set): `LoadAcceleratorsA` returns the
loaded accelerator table's VA; `TranslateAcceleratorA` parses its 8-byte `ACCEL`
entries and, on a matching virtual key, sends `WM_COMMAND(id, accelerator)` into the
game's `WndProc`. **Pressing F2 now starts a New Game** — the game responds and begins
loading a mission.

## A working `fscanf` — the game parses its text manifests

New Game opens `mission.ini` and parses it with `fscanf("%d\n")` / `fscanf("%s\n")`, but
the lifted MSVC scanf core reads through a lowio path that bypasses our host FILE layer,
so it never saw the file's bytes — every parse failed ("Bad mission file").

Host-shimmed `fscanf` (`sub_0045223E`): a compact reader over `host_file_getc` covering
`%d/%i/%u/%x/%o`, `%s`, `%c`, `%f/%g/%e`, plus literal and whitespace matching. The game
now parses its text manifests correctly.

## The mission format — and a synthesized `mission.ini`

`mission.ini` is a **campaign manifest**: a count, then one `.LVL` filename per mission.
A `.LVL` is the same shape — a count followed by its component files
(`atmos.txt`, `atmos.raw`, `atmos.clr`, `atmos.act`, `atmos.tex`, …), all of which exist
in our extracted data. It is **not shipped** (absent from the original install and the
PODs) — the game only ever reads it — so we synthesize a minimal one:

```
1
ATMOS.LVL
```

With it in place, New Game parses the manifest, opens `ATMOS.LVL`, reads its header, and
the mission-load loop (`sub_004263xx`) runs cleanly: for each mission it opens the level
file, reads the count + first component line, and stores it in the campaign table.

> `gamedata/mission.ini` is gitignored along with the rest of the data. Create it with
> the two lines above (or list more `.LVL` files for a fuller campaign).

## The "Select Mission" dialog — auto-pick mission 0

After building the campaign table, the start-new-game path (`sub_00426890` →
`sub_004263B0`) calls **`DialogBoxParamA(template 145, "Select Mission")`** and reads the
chosen mission index from `[0x45FCC0]`. With no real dialog, the index stayed unset and
the loader returned 0, aborting the new game. The `DialogBoxParamA` shim now special-cases
template 145: it writes `[0x45FCC0] = 0` (pick the first mission) so the load proceeds.

**With that, New Game loads the whole mission**: it opens ~70 gameplay assets — the player
ship and weapon/projectile/power-up/enemy `MODELS/*.BIN`, the `STATBAR.RAW` cockpit HUD,
`TITLE.RAW`, the level palettes, and `MUSIC/SEX.MOD` — then settles into a per-frame
tick-wait.

## Where it stands now

```
boot → assets → window → dismiss info dialogs → front-end
   → [F2] → WM_COMMAND(New Game) → mission.ini → ATMOS.LVL → Select-Mission(auto 0)
   → load ship/weapons/enemies/HUD models + music → (waits on game-loop worker)
```

The recompiled game **boots, takes input, starts a New Game, and loads an entire
mission's worth of real 3D models, HUD, palettes, and music.**

## Input is now fully plumbed (DispatchMessage → WndProc)

The post-load screens use a second input style: a message pump (`sub_00409BE0`:
`PeekMessage → TranslateAccelerator×2 → TranslateMessage → DispatchMessage`) whose loop
then polls a **key-available flag `[0x541F14]`** that *only the game's WndProc sets* on
`WM_KEYDOWN`. Our `DispatchMessageA` was a no-op stub, so dispatched keys never reached
the WndProc and these loops hung. Fixed: `DispatchMessageA` now routes the game's `MSG`
into the lifted WndProc (`host_dispatch_msg` → `call_game_wndproc`). Verified end-to-end —
an injected Enter now reaches the WndProc and sets `[0x541F14]`, which the game reads and
clears. Between this and the earlier PeekMessage delivery, **both of the game's input
styles work**. (A periodic Enter is injected after New Game to advance "press a key"
screens with no real keyboard focus.)

## The "2-colour screen" is the title — loaded correctly, then colour-collapsed

The persistent 2-colour image is **the Fury³ title screen** (`ART/TITLE.RAW`: "Microsoft
FURY³" over an explosion burst, planets, copyright). Confirmed by pixel-correlation: the
displayed buffer is a *perfect per-pixel remap* of TITLE.RAW (0 conflicts over its 166
indices). And `fread` delivers TITLE.RAW correctly — instrumenting the host `fread` shim
showed **166 distinct colours** read into the surface. So the front-end is *working*: it
reaches the title, loads the right art, and displays it.

## The remap is a 256-byte palette-translation table at 0x47C770 — built wrong

The generic image loader `sub_00420F60` (fread the .RAW, then a per-pixel loop at
`L_00420FC6`) remaps every pixel through a 256-byte table at **`0x47C770`**:
`pixel = table[pixel]`. The runtime table is `[0..31] → 10, [32..255] → 0` — a clean
threshold, collapsing the 166-colour title to black+grey. `0x47C770` is in `.bss`
(zero-init) and built at runtime by an FPU-heavy color/shade builder (`sub_00444D10`);
its input palette `0x4EB630` is the correct vga.act grayscale ramp, so the **builder
itself is producing the wrong table** (it should be ≈identity when an image's palette
matches the current palette, as the title's does).

**Confirmed by experiment:** forcing `0x47C770` to identity before the title load makes
the work buffer come out at **166 colours**, and rendering it produces the **pixel-perfect
full-colour "Microsoft FURY³" title screen**. So the entire front-end pipeline — load,
remap, snapshot, blit, present — is correct; only this one translation table is wrong.
(Forcing identity globally is *not* a fix: level images carry their own `.act` palettes
and need a real, non-identity translation — so the builder must be fixed, not bypassed.)

## SOLVED — it was a 9th lifter bug: `fstp st(i)` pop order

The `{0..31→10, 32+→0}` table is built by `sub_00443040`, which constructs a per-index
colour ramp from palette *ramp-range* pairs (`0x47E980`: groups `0-31, 32-63, …`), and
`sub_004432F0` composes it into `0x47C770`. Comparing `sub_00443040` against the **IDA
Hex-Rays decompilation of the original binary** showed its inner loop is a clean
accumulator: `byte[v7] = (int)v5; v5 += step`, ramping from `10.0`. Our output was flat
(no accumulation) — so the x87 stack was desyncing.

The culprit: the loop juggles the FPU stack with `fstp st(2)` / `fstp st(0)`, and the
lifter emitted `_st[i] = fp_pop()` for `fstp st(i)`. That's **off by one**: x86
`fstp st(i)` copies `ST(0)→ST(i)` *then* pops, so the stored value lands at `st(i-1)`;
popping first and then storing into `st[i]` desyncs the stack (and `fstp st(0)`, the
common "discard top", wrongly *kept* the popped value). Fixed in the lifter to
`{ _st[i] = _st[0]; fp_pop(); }` (pushed to pcrecomp `lift32.py`).

**Result: the recompiled Fury³ renders its 166-colour "Microsoft FURY³" title screen
correctly, through its own palette — no hacks.** This was the 9th lifter bug found on this
target (after the FPU `st(i)` base-index fix), and like that one it's an FPU-stack issue
that quietly corrupted floating-point math everywhere; expect other FP-heavy paths
(3D transforms, shading) to improve too.

## Current frontier

1. **Front-end → flight** — New Game preloads the mission (models/HUD/music) but returns
   to the front-end without loading level geometry (`atmos.raw`/`.tex`); trace
   `sub_00426890` → `sub_00401690`/`sub_00426690` for the launch path.
2. **Threads + audio** — the game `CreateThread`s an **audio-mixer** worker
   (`sub_00409B90` → `sub_0044A000` → `waveOutWrite`) we stub; running a second lifted
   thread needs thread-local registers. Then `waveOut` output.

## Artifacts

- pcrecomp `disasm32.py` — jmp/call-following function recovery (committed upstream)
- `run_lift.py` — `_recover_missing_funcs` (recovers thunk/tail-call targets IDA missed)
- `src/runtime/recomp_runtime.c` — input delivery (`host_peek_game_msg`,
  `host_present_tick`), accelerator translation (`host_translate_accel`), palette
  fallback (`host_load_act` + non-black guards)
- `src/runtime/shims_impl.c` — host `fscanf`, `PeekMessageA`/`GetMessageA` input
  delivery, `TranslateAcceleratorA`/`LoadAcceleratorsA`, `DialogBoxParamA` (auto-picks
  mission 0 for the "Select Mission" dialog), `DispatchMessageA` (routes to the WndProc)
