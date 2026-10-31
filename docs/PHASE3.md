# Phase 3 — Lift to C

**Status: ✅ complete (2026-06-12)**

Goal: turn the recovered x86 into C that compiles. Not running yet — compiling.
That's the milestone here: every function becomes a C function, and every
translation unit builds to an object file with zero errors.

## Method

A project-local driver, `run_lift.py`, adapting the proven pcrecomp linear-lift
pipeline (cf. `recoil/run_pipeline.py`) to Fury3:

- **Seeds from IDA with exact bounds.** Entries come from `analysis/ida_funcs.json`,
  and crucially we use IDA's real `end_ea` for each function instead of the usual
  "disassemble until the next entry" guess. Each function is decoded within its true
  `[start, end)` window — no bleed across data-in-code or padding.
- Each function is linear-disassembled (capstone), basic-block leaders marked, then
  lifted by the shared `lift32.Lifter` into the global-register-model C the pcrecomp
  runtime expects (`eax`/`ecx`/…, `MEM32(...)`, `PUSH32`/`POP32`, `fp_push`/`fp_pop`,
  `RECOMP_CALL`/`RECOMP_ICALL`/`RECOMP_ITAIL`).
- Output is chunked into `recomp_0000.c … recomp_0004.c` (≤400 functions each) plus
  `recomp_funcs.h` (prototypes) and `recomp_dispatch.c` (the dispatch table).

## Results

```
functions lifted : 1,932
lift errors      : 0
generated C       : 124,518 lines  /  6.1 MB  across 5 chunks
lift time         : ~1 second
```

### It compiles

All six generated translation units build to objects with **zero errors**
(gcc 15.2, `-c -w`):

```
recomp_0000.c … recomp_0004.c   ✅
recomp_dispatch.c               ✅   (1,932-entry dispatch table)
-----------------------------------
6 / 6 objects, 0 errors, 5.5 MB of object code
```

### The dispatch table solves indirect dispatch

Phase 2 flagged 100 functions reachable only through function-pointer tables.
`recomp_dispatch.c` emits a sorted `{ va, func }` table of all 1,932 entries; the
runtime's `RECOMP_ICALL`/`RECOMP_ITAIL` macros look up a target VA in it at call
time. So `call dword ptr [eax+4]` (a vtable/dispatch call) resolves to the right
lifted C function at runtime — no static edge needed.

### Imports are left as IAT indirections

Calls that reach a Win32 import become `RECOMP_ICALL(<IAT slot VA>)` /
`RECOMP_ITAIL(...)` — an indirect call through the import address table. Phase 4
populates those 160 slots with the SDL2-backed shims. The lift draws the boundary
exactly where we want it.

## A pcrecomp runtime bug, found and fixed

Two chunks initially failed to compile:

```
recomp_0001.c: error: implicit declaration of function 'POP32'
recomp_0004.c: error: implicit declaration of function 'POP32'
```

The lifter emits a two-arg `POP32(sp, dest)` for `pop r/m32` (pop into a memory
operand), but `recomp_types.h` only defined `POP32_VAL` (the value-returning form)
and `POPAD`. The two-arg macro was simply missing — earlier projects happened never
to lift a `pop [mem]`. Fixed in the pcrecomp runtime:

```c
#define POP32(sp, dest) do { (dest) = POP32_VAL(sp); } while(0)
```

Defined via `POP32_VAL`, so it inherits the GCC statement-expression / MSVC
inline-function split automatically, and works whether `dest` is a register alias
or `MEM32(...)`. Re-vendored; all six chunks then compiled clean.

## Honest scope — what compiles is not yet what runs

This phase delivers the **function bodies**. To get a running program we still need
(Phases 4–6):

- the runtime translation unit: definitions of `g_eax…g_edi`, `g_esp`, `g_mem_base`,
  and a loader that maps the original PE image (data sections, `.rsrc`) into memory so
  `MEM32(absolute_va)` reads real bytes;
- the `RECOMP_CALL` dispatch glue and stack/entry setup;
- the 160 IAT slots filled with SDL2-backed shims (Phase 4);
- a `WinMain` entry trampoline.

None of that is wired yet — but the hard, bulk artifact (124K lines of correct,
compiling C for all 1,932 functions) is done.

## Artifacts

- `run_lift.py` — the Fury3 lift driver (committed)
- `src/recomp/recomp_types.h` — vendored pcrecomp runtime header, now with `POP32` (committed)
- `src/recomp/gen/` — 6.1 MB of generated C (gitignored; regenerate with `run_lift.py`)
- `analysis/phase3_codegen.json` — codegen stats (gitignored)

## Tooling changed in pcrecomp this phase

- `runtime/recomp32/recomp_types.h` — added the missing two-arg `POP32(sp, dest)`
  stack macro (`pop r/m32` to a memory destination).
