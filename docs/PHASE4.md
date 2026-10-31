# Phase 4 — Shim Layer + Runtime (first link)

**Status: ✅ complete (2026-06-12)** — it links, and the runtime initializes.

Goal: wrap the 124K lines of lifted C from Phase 3 with everything needed to make
a real, linkable program — the register/memory runtime, indirect-call dispatch, and
a bridge for every Win32 import — and get it to **link into an executable**.

## What got built

### 1. The runtime (`src/runtime/recomp_runtime.c`)

- **Register model**: definitions of `g_eax…g_edi`, `g_esp`, segment regs, `g_mem_base`.
- **PE image loader**: parses the original `FURY3.EXE` section headers *at runtime*
  and maps the whole image at its real VA (`0x00400000`) with `VirtualAlloc` fixed,
  copying each section's raw bytes to its VA and zeroing `.bss`. Because the host exe
  is linked at a high base (`0x70000000`), the game's VA range is free, so the mapping
  is 1:1 and `g_mem_base` stays 0 — `MEM32(va)` reads the real byte at `va`.
- **Simulated stack**: 1 MB at `0x00200000`, `ESP` set to the top.
- **Dispatch**: `recomp_lookup` (binary search over the 1,932-entry table),
  `recomp_lookup_import` (the bridge table), and a VEH handler that dumps register
  state + the indirect-call ring buffer on an access violation.

### 2. The import bridge layer (`gen_imports.py` → `src/runtime/imports_gen.c`)

A shim for all **160 imports** across GDI32 / KERNEL32 / USER32 / WINMM / comdlg32.
Each shim runs inside the recomp stack model: it reads arguments from the simulated
stack, returns via `EAX`, and — crucially — pops the right number of stdcall argument
slots so **the simulated stack stays balanced** even while the shim is still a stub.
`gen_imports.py` carries the stdcall arg-count for every one of the 160 APIs.

### 3. IAT self-patching (the trick that makes both call paths work)

The lifted code reaches an import two ways:

```
direct call ds:[__imp_X]   ->  RECOMP_ICALL(slot_va)        -> lookup_import(slot_va)
thunk jmp ds:[__imp_X]      ->  RECOMP_ITAIL(MEM32(slot_va)) -> lookup_import(MEM32(slot_va))
```

So at init we **write each IAT slot's own VA into the slot**
(`MEM32(slot) = slot`). Now `MEM32(slot_va) == slot_va`, and *both* paths land in
`recomp_lookup_import(slot_va)` → the right bridge. 160 slots patched.

## It links

```
$ gcc -O1 -w -I src/recomp -I src/recomp/gen -I src/runtime \
      src/runtime/recomp_runtime.c src/runtime/imports_gen.c src/recomp/gen/recomp_*.c \
      -Wl,--image-base,0x70000000 -o build/fury3.exe -lgdi32 -luser32 -lwinmm -lcomdlg32

  exit 0   ->   build/fury3.exe  (1.9 MB)
```

A `CMakeLists.txt` captures this. The whole thing — runtime + 160 bridges + 1,932
lifted functions — links with **zero errors**.

## And it initializes

```
$ ./build/fury3.exe analysis/FURY3.EXE
=== Fury3 Static Recompilation - runtime ===
[*] image: 7 sections, span 0x17C000 mapped at 0x00400000
[*] stack: 0x00200000..0x00300000 (ESP=0x002FFFC0)
[*] dispatch table: 1932 functions
[*] import bridges: 160 (160 IAT slots patched)
[*] runtime initialized.
```

The memory model is real: the original image is mapped, the dispatch and bridge
tables are live, the stack exists. This is the foundation everything else runs on.

## First contact with the entry point

Running with `--run` calls the recompiled entry point (`sub_00453081`, the CRT
startup that leads to `WinMain`). It **executes lifted code** and faults early —
inside CRT startup, before any indirect call is logged:

```
=== Access Violation at 0x6ff2d9c8 ===
  EAX=00000000 ECX=00000000 ... ESP=002FFFBC ...
```

That's the **Phase 6 (bring-up)** starting point, not a Phase 4 defect. Early MSVC
CRT startup pokes the TIB via the `fs:` segment, queries `GetVersion`, and sets up
the heap — prime suspects are segment-relative memory access and uninitialized
register state on entry. Chasing that fault is the next phase's job; the VEH handler
and ICALL ring buffer are already in place to do it.

## Honest status

- ✅ Links into a single executable, 0 errors.
- ✅ Runtime initializes: image mapped, stack up, dispatch + 160 bridges live.
- ✅ The recompiled entry point runs lifted code (until an early CRT-startup fault).
- ⬜ Doesn't reach `WinMain` / open a window yet — that's Phase 6.
- ⬜ Shims are stack-correct stubs; the render/audio/input bodies (DIB→SDL_Texture,
  waveOut→SDL_AudioStream, PeekMessage→SDL_PollEvent) get filled in Phases 6–7.

## Build artifacts

- `src/runtime/recomp_runtime.c` — runtime + loader + dispatch (committed)
- `src/runtime/imports.h` — bridge helper macros (committed)
- `src/runtime/imports_gen.c` — 160 generated shims + bridge/IAT tables (committed)
- `gen_imports.py` — the bridge generator (committed)
- `CMakeLists.txt` — reproducible build (committed)
- `build/` — output (gitignored)
