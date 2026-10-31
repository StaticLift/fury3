# Phase 2 — Classification

**Status: ✅ complete (2026-06-12)**

Goal: turn 739 anonymous functions into a *map* — who calls whom, which functions
touch the OS, where the 3D math lives, what the per-frame hot path is, and which
functions we should never bother lifting.

## Method

Exported the call graph, import usage, and x87 FPU density from IDA with a new
pcrecomp tool, `ida/ida_xrefs.py`, then classified locally
(`analysis/classify.py`). Inputs: `ida_funcs.json` (Phase 1) + `ida_xrefs.json`.

### The incremental-linking gotcha

First pass produced a nonsense call graph — every real function looked like it was
called once or twice, while a handful of 5-byte stubs racked up all the fan-in.
That's the signature of an **MSVC `/INCREMENTAL` build**: every `call` targets a
jump thunk (`call thunk; thunk: jmp realfunc`), so the thunks absorb the entire
call graph. **721 of the 881 thunks** are exactly these.

The fix: collapse `thunk → jmp target` edges before computing reachability and
fan-in. With thunks resolved, the true hot path falls right out. (Noted for
pcrecomp's general classifier — incremental-link thunk resolution should be a
standard pre-pass.)

## The shim boundary is 58 functions wide

Only **58 of 739** real functions touch the operating system at all. Everything
else is self-contained computation. That 58-function ring *is* the entire porting
surface — replace it and the game is platform-independent.

| Subsystem | Funcs | The APIs behind them |
|---|---:|---|
| `glue:window` | 28 | CreateWindowExA, RegisterClassA, DialogBoxParamA, MessageBoxA, GetSystemMetrics … |
| `glue:render` | 12 | **CreateDIBSection, StretchBlt, BitBlt, CreatePalette, SetDIBColorTable**, GetDC … |
| `glue:io` | 9 | CreateFileA, ReadFile, GetPrivateProfile*, FindResourceA, GlobalAlloc … |
| `glue:audio` | 7 | **waveOutOpen/Write/Reset**, timeGetTime … |
| `glue:input` | 2 | PeekMessageA, DispatchMessageA, SetCursorPos, ShowCursor … |

The remaining 681 functions:

| Class | Funcs | Meaning |
|---|---:|---|
| `math3d` | 26 | FPU-dense — the 3D transform / projection / lighting core |
| `logic`  | 655 | Pure game logic, world state, asset parsing, helpers |

## The hot path (thunk-resolved fan-in)

The most-called real functions — the per-frame workhorses to lift first and get right:

```
0x0041AEC0  called 77×    41 B   logic        ubiquitous leaf helper (fixed-point / accessor)
0x0041AF00  called 43×   149 B   logic
0x0043E940  called 37×   661 B   logic
0x0040BCC0  called 35×   109 B   glue:render  per-object draw/blit primitive
0x00431470  called 35×   522 B   logic
0x0041DCC0  called 33×    71 B   logic
0x00430C30  called 31×    51 B   logic
0x004317B0  called 29×  3101 B   logic        ← 2nd-largest fn in the binary + hot = main update
0x004327E0  called 29×   209 B   logic
0x0042D9B0  called 28×   303 B   logic
```

## The 3D math core (FPU density)

The floating-point heavy functions — the transform/projection math that defines the
voxel-flight look. Lift these carefully and verify x87 stack discipline:

```
0x0042E250   81 FPU ops   density 0.87   334 B   ← almost pure FPU: a vec/matrix kernel
0x0042E530   72 FPU ops   density 0.42   634 B
0x0042A750   54 FPU ops   density 0.40   411 B
0x0042A550   53 FPU ops   density 0.40   403 B
0x0042E140   53 FPU ops   density 0.74   214 B
0x00434570   49 FPU ops   density 0.41   379 B
```
The `0x42Axxx`/`0x42Exxx` cluster is tightly grouped — likely one source module of
vector/matrix routines. A good unit to lift and test together.

## Two things Phase 3 must handle

1. **Indirect dispatch.** 100 of the 739 real functions are *unreachable by direct
   calls* — they're only invoked through function-pointer tables in `.data`/`.rdata`
   (classic object/enemy `update()` vectors). Phase 3 must recover those pointer
   tables so the lifter wires indirect `call [table+i*4]` sites to real targets.
2. **CRT skip-list.** 313 FLIRT-identified library functions (`_sprintf`, `_malloc`,
   `__ftol`, `memcpy`, float helpers …) are written to `classification.json` →
   `flirt_skiplist`. The lifter shims these to the host CRT instead of re-deriving
   them — that's ~45 KB of code we never touch.

## Lift order for Phase 3

1. The `math3d` cluster (`0x42Axxx`/`0x42Exxx`) — leaf-ish, testable in isolation.
2. The hot leaf helpers (`0x41AEC0` & friends) — everything depends on them.
3. `glue:render` (12) + the DIB/palette path — needed to see any pixels.
4. Work up the call graph from `WinMain` toward `sub_4317B0` (the main update).
5. Recover indirect-dispatch tables, then lift their 100 targets.

## Artifacts (not committed — derived from your binary)

- `analysis/ida_xrefs.json` — call graph + import usage + FPU density per function
- `analysis/classification.json` — per-function class/subsystem/fan-in/FPU + CRT skip-list
- `analysis/classify.py` — the (project-local) classification script

## Tooling added to pcrecomp this phase

- `tools/ida/ida_xrefs.py` — **new**: exports call graph, import-usage, and x87 FPU
  density from IDA. The classification substrate for any lift.
