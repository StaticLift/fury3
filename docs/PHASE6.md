# Phase 6 — Bring-up (in progress)

**Status: 🟡 in progress (2026-06-12). `WinMain` is reached.** After fixing four
fundamental lifter bugs, the recompiled binary runs the whole MSVC CRT startup
*and enters the game's `WinMain`* (`sub_0040BA90`), which executes real init
(current-directory, module-path, building its help-file path). It currently faults
inside the CRT `sprintf` (`__output`) that `WinMain` calls — a format loop that
doesn't terminate. We went from *faulting on the very first instruction* to *inside
the game's own entry point*.

## Update — reaching WinMain (two more lifter fixes)

Two more general lifter bugs, found pushing through CRT init into WinMain:

### 4. Conditions read clobbered operands (flag snapshots)

A `jcc` reads flags from an earlier `cmp`/`test`/`sub`/…, but the lifter stored the
operand *expressions* and re-evaluated them at the jump. Any instruction in between
that wrote an operand corrupted the condition. The exact case that blocked us:

```c
RECOMP_ICALL(...VirtualAlloc...);
eax = 0;                            /* mov eax, 0 */
if (TEST_NZ(eax, eax)) goto ...;   /* jne -- tested eax AFTER it was zeroed! */
```

Fixed by snapshotting flag operands into `_flag_a`/`_flag_b` temporaries at the
flag-setter, so the condition is immune to intervening writes. (Pushed to pcrecomp.)

### 5. Intra-function jump tables (switch statements)

A `switch` lifts to an indirect `jmp [table + i*4]` → `RECOMP_ITAIL(<runtime va>)`,
but the global dispatch only knows *function entries* — it can't jump to a label
*inside* the current function, so the switch broke (e.g. the `sprintf` state
machine). Fix (in `run_lift.py`): any function containing an indirect jump gets a
label on **every** instruction plus a local dispatch — a standard-C `switch` over
all those labels (`goto`), falling back to the global `RECOMP_ITAIL` only for real
cross-function tail calls. Jump tables now resolve.

### Function-entry tracer

Added `RECOMP_ENTER(va)` (no-op unless `-DRECOMP_TRACE`): each lifted function
records its VA into a ring buffer, dumped on a crash or `ExitProcess`. With no
working debugger (the MSYS2 gdb is missing a DLL) this is our backtrace — it's how
we pinned every fault above to an exact function.

### Where it stands

```
[BRINGUP] >>> game WinMain (0x0040BA90) entered <<<
[import-stub] SetCurrentDirectoryA
=== Access Violation: write to 0x0057C000 ===   (CRT __output / sprintf overrun)
```

`WinMain` calls `sprintf(buf, "%s\\%s", moduleDir, "fury3.hlp")`. The lifted MSVC
`__output` state machine (char-class + state tables + a jump table at the function
end) is gnarly to debug without a debugger, so we **host-shimmed `_sprintf`**
instead — a clean technique for well-known CRT functions.

### Host-shim mechanism + `sprintf`

`run_lift.py` now has a `HOST_SHIM` set: listed VAs are *not* lifted; the generator
emits an `extern` decl and the dispatch table points at a hand-written body in
`shims_impl.c`. `sub_00450AE7` (`_sprintf`) is the first: a mini-`printf` that reads
the format + varargs from the simulated stack, formats each conversion with the host
CRT, and writes into simulated memory. This sails past the `__output` overrun.

### Current frontier — WinMain command-line copy

With `sprintf` shimmed, `WinMain` runs further and now overruns in its own
command-line handling:

```
[BRINGUP] >>> game WinMain (0x0040BA90) entered <<<
=== Access Violation: write to 0x0057C000 ===   (WinMain cmdline copy)
```

`WinMain` copies `lpCmdLine` (stripping `"` quotes) into a global buffer at
`0x5328C0`, looping `strlen(lpCmdLine)` times. `lpCmdLine` is a heap pointer
(`~0x10000009`) that isn't NUL-terminated, so the copy runs ~0x49000 bytes off the
end of the image. Root cause is upstream: the CRT's `argv`/command-line setup
(`_setargv`) computed `lpCmdLine` pointing into the wrong (or unterminated) heap
buffer — itself a consequence of our stubbed command-line/heap path.

**Next:** either host-shim the CRT command-line setup, or make `GetCommandLineA` +
the `argv` path produce a correctly-terminated, well-formed command line so
`lpCmdLine` is a valid (likely empty) string. Then WinMain proceeds toward
`RegisterClassA`/`CreateWindowExA` — the first window (Phase 7).

## Update — the game boots all the way into its main loop

Two more general lifter fixes took it from "stuck before the window" to "running its
full startup and main loop."

### 6. `repne scasb` / `repe cmpsb` (strlen/strcmp) miscompiled

The `strlen` idiom `repne scasb; not ecx; dec ecx` was lifted as a *pre-test* loop:

```c
while (ecx && LO8(eax) != MEM8(edi)) { edi += _df; ecx--; }   // WRONG
```

Real `repne scasb` is a do-while: it processes (and decrements ECX for) the matching
byte too. For an **empty string** the pre-test version never decrements ECX, so
`strlen("")` came out as `0xFFFFFFFF` — and WinMain's command-line copy looped ~300 KB
off the end of the image. Fixed to the correct do-while semantics, with proper ZF for
a following `je`/`jne`. `lpCmdLine` (empty) now measures 0 and the copy is skipped.
This was the actual command-line overrun above. (Pushed to pcrecomp.)

### 7. The x87 FPU stack must be GLOBAL, not per-function

The model declared `_st[8]` / `_fp_top` / `_fpu_cw` as **per-function locals**. But
the x87 stack is a single shared 8-register stack, and CRT helpers like `__ftol`
*receive their argument on it from the caller*. With per-function stacks, `__ftol`
saw an empty stack and returned 0 — so the game's frame timer
(`get_time = (int)(timeGetTime()*1179.648)` via `fild`/`fmul`/`__ftol`) was always 0,
and the main loop's frame-wait `while (get_time() < target)` spun forever. Made
`_st`/`_fp_top`/`_fpu_cw` global (`g_st`/`g_fp_top`/`g_fpu_cw`). (Pushed to pcrecomp.)

### How far it boots now

With those fixes plus a real `timeGetTime` (host milliseconds) and friendlier
success returns for non-fatal startup steps, the recompiled game runs its **entire
boot sequence**:

```
WinMain → FindWindow → LoadCursor/Icon → RegisterClassA → CreateWindowExA →
ShowWindow → LoadAccelerators → [frame-timing loop runs] → reads config
(GetPrivateProfile*) → display setup (GetDeviceCaps, GetDC) → message loop
(PeekMessageA) → CreateThread (sound) → ...
```

26 distinct OS APIs deep, into the game's main loop and subsystem init. The
`MessageBox` shim surfaced the game's own diagnostics along the way ("Cannot load
accel!", "Unable to write .INF", "Unable to create sound thread") — each fixed by
returning success from the relevant stub.

### Current frontier (→ Phase 7)

It now faults on a **null read inside the lifted `__output`**, reached from a
sprintf-family path with a null `%s` argument — downstream of **stubbed resource /
asset / file loading** (the game has null pointers where loaded data should be). The
fix is Phase 7 proper: implement the real subsystems — file/resource I/O so assets
load, and the DIB/`StretchBlt` render path + palette so frames reach the screen.
That's where the first pixels come from.

**Phase 6 is essentially complete: the recompiled binary boots end-to-end into its
main loop.**

---

## Original Phase 6 writeup (CRT startup)

The recompiled binary executes the entire MSVC CRT startup.

## Where we started

`--run` called the entry point and faulted immediately:

```
mov eax, dword ptr fs:[0]   ->  eax = MEM32(0);   // read VA 0 -> access violation
```

## Two fundamental lifter bugs fixed (pushed to pcrecomp)

### 1. `fs:`/`gs:` segment overrides were dropped

The lifter's memory-operand formatter ignored the segment register, so `fs:[0]`
(the TIB / SEH chain head that MSVC CRT startup reads first) lifted to `MEM32(0)`
— a null read. Fixed `_fmt_mem_addr` to route `fs:`/`gs:` accesses through a
runtime base:

```c
fs:[disp]  ->  MEM32(FS_BASE + (disp))
```

The runtime now allocates a **simulated TIB** and points `FS_BASE` at it
(ExceptionList = -1, StackBase/StackLimit, Self). `cs/ds/es/ss` stay flat.

### 2. `ret` didn't pop the return address — ESP drifted 4 bytes per call

`RECOMP_CALL` pushes a dummy return address, but the lifted `ret` emitted just
`return;` (and `ret N` emitted `esp += N`) — **neither popped the 4-byte return
address**. Every call drifted the simulated ESP down by 4, and after a few hundred
CRT calls the dummy sentinel got read as a function argument (we caught a
`VirtualAlloc(size = 0xDEAD1000)`). Fixed:

```
ret      ->  esp += 4;       return;
ret N    ->  esp += 4 + N;   return;
```

### 3. Configurable return sentinel

Made the dummy return address a `RECOMP_RETADDR` macro (default unchanged at
`0xDEAD0000` for other projects). Fury3 builds with `-DRECOMP_RETADDR=0u` so that
if a value ever still leaks, it's a benign `0` instead of a multi-gigabyte size.

## The CRT-bootstrap shim layer

With the lifter fixed, CRT startup ran far enough to depend on real Win32 behavior.
Hand-wrote ~22 shims (`src/runtime/shims_impl.c`) plus a simulated heap:

- **Heap**: a 128 MB region with a page-aligned bump allocator backing
  `VirtualAlloc`/`GlobalAlloc` (the CRT small-block heap reserves & commits through
  these).
- **Process/env**: `GetVersion`, `GetCommandLineA`, `GetModuleFileNameA`,
  `GetModuleHandleA`, `GetStartupInfoA`, `GetEnvironmentStrings`,
  `GetCurrentDirectoryA`, `GetACP`/`GetOEMCP`/`GetCPInfo`.
- **I/O**: `GetStdHandle`, `GetFileType`, `WriteFile` (→ host stdout),
  `GlobalMemoryStatus`.
- **`ExitProcess`** actually terminates (so we observe clean exits instead of
  running into garbage).

## How far it gets now

```
[*] runtime initialized.
[*] calling entry point sub_00453081 ...
[import-stub] GetLastError
[ExitProcess] game called ExitProcess(0) - terminating
```

The CRT startup runs to completion: heap init (multiple `VirtualAlloc`
reserve/commit pairs), `_ioinit`, command-line/environment setup, and the
`_initterm` initializer tables — hundreds of instructions, all dispatch and import
bridging working. Then it calls `ExitProcess(0)`.

**`WinMain` (`sub_0040BA90`) is confirmed *not yet* entered** — a runtime
manual-override hook on its VA (`recomp_lookup_manual`) never fires. So a CRT-init
path diverts to `exit()` before the `WinMain` call at `0x4531A4`.

## The map to `WinMain`

We traced the exact startup chain so the next session starts with a map:

```
sub_00453081  _mainCRTStartup  (heap/io/argv/env/_cinit, then:)
  GetModuleHandleA -> hInstance
  call sub_00401A05  (thunk) -> jmp sub_0040BA90   <-- the game's WinMain
  push eax (WinMain return)
  call sub_00451FF2  -> sub_00452040  doexit -> ExitProcess
```

`WinMain` (`sub_0040BA90`) itself is recovered and readable — it builds its module
path, strips the filename, `sprintf`s, and parses the command line before (in the
original) creating its window class.

## Next

1. Find the CRT-init function that diverts to `exit()` before `0x45319E` (suspect:
   a `_initterm` initializer hitting a stubbed dependency, or a residual
   control-flow/stack-precision case). A working debugger (the MSYS2 gdb is missing
   a DLL) or a direct-call tracer is the tool.
2. Once `WinMain` runs: implement the file-I/O shims (`CreateFileA`/`ReadFile`/
   `GetPrivateProfile*`) against the real game directory so it can load its config,
   then `RegisterClassA`/`CreateWindowExA` → the first window (Phase 7).

## Artifacts

- `src/runtime/shims_impl.c` — hand-written CRT-bootstrap shims (committed)
- `src/runtime/recomp_runtime.c` — TIB + heap + WinMain hook (committed)
- Build: add `-DRECOMP_RETADDR=0u`; see CMakeLists / docs.

## Pushed to pcrecomp this phase

- `lift/lift32.py` — `fs:`/`gs:` segment routing; `ret`/`ret N` now pop the return address.
- `runtime/recomp32/recomp_types.h` — `FS_BASE`/`GS_BASE`; configurable `RECOMP_RETADDR`.
