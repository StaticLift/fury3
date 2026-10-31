# Phase 1 — Disassembly & Function Recovery

**Status: ✅ complete (2026-06-12)**

Goal: a complete, correct instruction-level map of `FURY3.EXE`'s code, with
recovered function boundaries to drive classification and lifting.

## Method

Two independent passes, cross-checked:

1. **pcrecomp `disasm32.py`** — pure-Python recursive-descent disassembly (capstone),
   no external tooling. Fast to run anywhere, our portable baseline / fallback.
2. **IDA Pro 9.1 (idalib, headless)** via pcrecomp `ida/ida_export.py` + a new
   `ida/ida_funcs.py` — authoritative function recovery with FLIRT library
   identification and proper data-in-code separation.

IDA is the source of truth; `disasm32` is the no-IDA cross-check.

## Results

| Metric | IDA (authoritative) | disasm32 (baseline) |
|---|---:|---:|
| Functions recovered | **1,932** | 950 |
| Instruction heads | 86,782 | 74,564 |
| Code bytes (real) | 296,874 | — |
| Code ranges (data-in-code splits) | 1,086 | — |

`.text` is 383 KB of file space but only ~297 KB is actual code — the other ~86 KB
is **data-in-code**: jump tables, string/constant tables, and embedded palettes
interleaved with instructions. The 1,086 separate code ranges are exactly why a
naive linear sweep mis-decodes this binary and why we lean on IDA's classification.

### The lift workload

Of the 1,932 functions IDA found, most are not hand-written game code:

| Bucket | Count | Bytes | Disposition |
|---|---:|---:|---|
| **FLIRT library (CRT/runtime)** | 313 | 45,383 | **Shim**, don't lift (recognised MSVC 2.x CRT) |
| **Thunks** | 881 | — | Trivial single-jump wrappers (mostly import stubs) |
| **Real lift targets** | **739** | **234,160** | The actual game — renderer, physics, AI, mission, I/O |

IDA also auto-named **204** functions from imports/FLIRT, giving us free anchors.

### Where the engine lives (size distribution of the 739 real targets)

| Size class | Count | Bytes |
|---|---:|---:|
| tiny (<64 B) | 193 | 5,061 |
| small (64–256 B) | 274 | 37,372 |
| medium (256 B–1 KB) | 213 | 100,927 |
| large (1–4 KB) | 59 | 90,800 |
| huge (>4 KB) | 0 | 0 |

**No monster functions.** The largest is 3,211 bytes. The renderer/transform/physics
core is concentrated in the ~272 medium+large functions (~192 KB); the ~467 tiny+small
functions are helpers and accessors that will lift almost mechanically.

### Top engine-core candidates (largest real functions)

```
0x0041E250   3,211b      0x00424E60   2,042b
0x004317B0   3,101b      0x0040C2B0   1,970b
0x0042EBA0   3,098b      0x00403C83   1,949b
0x00415480   2,428b      0x004394E0   1,882b
0x00410930   2,396b      0x0043FA90   1,874b
0x0040509E   2,118b      0x0040629F   1,793b
0x00404420   2,081b      0x00429770   1,683b
```

These get human eyes first in Phase 3 — they're almost certainly the 3D transform,
the span rasterizer, collision, and the main per-frame update.

## Takeaways for Phase 2

- **739 functions to lift, none larger than ~3.2 KB.** Very tractable.
- 313 CRT functions become shims, not lifts — wire FLIRT names into the lifter's
  skip-list so we don't waste effort re-deriving `memcpy`/`malloc`/float helpers.
- 881 thunks collapse to direct calls / import-table entries.
- Next: build the call graph from the entry point, tag the 739 into subsystems
  (render / math / AI / I/O / audio / UI), and name the hot path. See PLAN Phase 2.

## Artifacts (not committed — derived from your binary)

- `analysis/ida_codemap.json` — per-segment functions / heads / code-ranges (`--key va`)
- `analysis/ida_funcs.json` — function catalog: name, bounds, size, FLIRT/thunk flags
- `analysis/functions.json` — disasm32 baseline catalog + stats

## Tooling added to pcrecomp this phase

- `tools/disasm/disasm32.py` — added a real argparse **CLI** (the README documented
  it but it was library-only) + honest union/clamped byte-coverage reporting.
- `tools/ida/ida_funcs.py` — **new** function-catalog exporter (name/bounds/size +
  FLIRT-library / thunk / no-return flags) for lift planning and triage.
