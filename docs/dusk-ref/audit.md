# ww HLE patches audited against dusk canonical sources

Every HLE patch we apply in ww is replacing a Nintendo framework or JSystem
function. With dusk's decomp source as the canonical reference (TP shares this
code nearly verbatim with WW), we can see exactly what each patch is supposed
to do and where ours deviates. This doc lists every active HLE patch and what
the canonical source teaches us.

## The scene creation pipeline (clarified)

What we've been treating as a single black box (`func_80022CEC = "scene create"`)
is actually a 6-phase state machine driven from two queues. The canonical
sources:

- `src/f_op/f_op_scene_req.cpp::fopScnRq_Request` ≡ entry point (our `func_8000AC3C`)
- `src/f_pc/f_pc_node_req.cpp::fpcNdRq_Request` ≡ generic node create-request
- `src/f_pc/f_pc_stdcreate_req.cpp::fpcSCtRq_*` ≡ the 6-phase pipeline:
  1. `phase_Load` — `fpcLd_Load(procname)` loads a **REL module** from
     `/dvd/<name>.str`. This is where `/dvd/framework.str` comes from. For
     statically recompiled code, this should be a no-op return success.
  2. `phase_CreateProcess` — `fpcBs_Create` allocates and initializes the
     `base_process_class`. **This is almost certainly the real `func_80022CEC`.**
  3. `phase_SubCreateProcess` — `fpcBs_SubCreate` runs the profile's create
     method (per-process init logic).
  4. `phase_IsComplete`
  5. `phase_PostMethod` — calls the post-create function pointer.
  6. `phase_Done`

Our current HLE for `func_80022CEC` short-circuits all of this by manually
populating a `0xF8` byte scene_class object with magic values from Dolphin
captures and setting `init_state=2` (executing). It works but bypasses phases
3-5 — meaning the scene's per-profile create logic (which sets up game state)
never runs.

## HLE patch ↔ canonical source map

| our HLE | canonical | deviation | severity |
|---|---|---|---|
| `func_80022CEC` (scene create, recomp_0008 + main.cpp) | `fpcBs_Create` in `src/f_pc/f_pc_base.cpp` | We short-circuit `init_state=2`. Canonical sets `init_state=0` and lets `fpcSCtRq_phase_SubCreateProcess` run the per-profile create. | **High** — explains why our scene has no per-profile initialization (no actors, no kankyo setup, etc.) |
| `func_802B6FEC` (getGlbResource, jkr_archive.cpp:326) | `JKRFileLoader::getGlbResource` in `jsystem/JKernel/src/JKRFileLoader.cpp` | We do prefix `strnicmp` + fall back to first archive. Canonical does exact `strcmp` after `fetchVolumeName` (tolower-normalized) and returns NULL on miss. | Medium — works because game tolerates NULLs in many call sites; masks where REL/disc lookups should happen. |
| `func_802B6AB8` (findVolume, jkr_archive.cpp:376) | `JKRFileLoader::findVolume` in `jsystem/JKernel/src/JKRFileLoader.cpp` | Same as above, plus: canonical returns `sCurrentVolume` when path doesn't start with `/`. We always search. | Medium — same as above. |
| JKR vtable[3] unmount, vtable[5] getResource | `JKRFileLoader::unmount`, `JKRMemArchive::getResource` | Need to compare against the canonical `JKRMemArchive` lookup. | Unknown until checked. |
| `func_802B0434` (JKR alloc, recomp_0063) | `JKRHeap::alloc` in `jsystem/JKernel/src/JKRHeap.cpp` | We bump-allocate at 32B align. Canonical is an expanding-heap allocator. | Low — bump is fine for now; will need real heap when freeing matters. |
| `func_8001199C` (getCurrentHeap, recomp_0001) | `JKRHeap::sCurrentHeap` getter | We return fixed `0x80400010`. Canonical reads a static. | Low — works because we never free. |
| `func_802C7788` (VIWaitForRetrace, recomp_0066) | Dolphin SDK `VIWaitForRetrace` | No-op vs. proper VI emulation. | Low for now — we run uncapped. |
| `func_802CDCB4` (GX camera matrix FIFO, recomp_0067) | Internal GX FIFO write helper | No-op vs. proper FIFO interpretation. Aurora handles this at the GX API layer. | Medium — blocks proper camera once we hook the renderer. |
| `func_802CB8D0` (OSReport, recomp_0067) | Dolphin SDK `OSReport` | No-op. Canonical writes to OS console (Aurora forwards to stdout). | Low. |
| `func_80022CEC` placeholder + `func_80040050/068` (main.cpp:645/653) | List iterator predicates | Inferred-correct comparators. Canonical equivalent is `cTg_*` / `cLs_*` in `SSystem/SComponent`. | Low. |

## Concrete fixes unlocked by the canonical sources

Ordered by expected payoff. Each item is a discrete code change.

### 1. Fix `func_80022CEC` to call the real create method (high payoff) — ATTEMPTED 2026-05-12

The most impactful fix. Right now we build a process header but never call the
profile's create method (`pprofile->methods->create_method`). That method is
what sets up scene-specific state: actor lists, kankyo, room manager, etc.

**Implemented:** behind `WW_DRIVE_CREATE_METHOD=1` in `src/main.cpp`. The HLE
now reads `methods` at `proc+0xA8`, then `create_method` at `methods+0x00`,
sets `r3=proc_addr`, and calls via `g_func_table.call`. With `init_state=0`,
the canonical `fpcNd_Create` (= `func_8003DD8C`, verified via recomp_0006.cpp)
runs its layer-init branch then falls through to per-profile `fpcMtd_Create`.

**Result: phase=4 (cPhs_COMPLEATE_e) returned, no crash, zero visible effect.**

**Root cause:** discovered the "dual reality" architecture. The processes that
actually dispatch each frame come from the Dolphin runtime state dump (already
linked into sublayer priority lists). Our HLE-synthesized scene at
`0x804061A0` is allocated but never linked into any priority list, so the
framework's per-frame iterator never sees it — running its create_method
correctly is invisible. See `memory/project_dual_reality.md`.

**To make this fix matter, we first need:**
- Either implement `fpcEx_ToExecuteQ` equivalent to link our process into a
  sublayer priority list, OR
- Stop double-allocating and have `func_80022CEC` return the existing
  Dolphin-preloaded scene pointer.

**Resolved 2026-05-12 (option 2)** via `WW_REUSE_BOOT_SCENE=1` env var.
`func_80022CEC` now returns `g_boot_scene_proc` (the scene allocated and
linked into sublayer1.listA + exec queue during boot init at main.cpp
~line 1087). Verified: caller's `item+28` is now 0x804001E0 (the
dispatching scene), not a parallel orphan. With `WW_DRIVE_CREATE_METHOD=1`
also set, the per-profile create_method runs successfully on the canonical
proc and returns `cPhs_COMPLEATE_e`. No regression — game still runs to
frame 300+.

Function IDs confirmed via recompiled disasm:
- `func_8003D788` = `fpcEx_ToExecuteQ`
- `func_8003D690` = `fpcEx_ToLineQ`
- `func_8003DF4C` = `fpcLyTg_ToQueue`
- `func_8003DD8C` = `fpcNd_Create` (already known)

**Remaining limitation:** actor-spawning is a separate pipeline (room.dzr →
`fopAcM_create` → fast-create-request queue). Driving the scene's
create_method does NOT spawn actors — that's audit item #6 (new).

Leave both env-var gates in place. They could be defaulted on after a
broader regression test, but the diagnostic value of being able to disable
each path is worth keeping.

### 2. Implement `fpcLd_Load` as a no-op success (low effort, removes a warning)

`func_80022CEC` is preceded by `fpcSCtRq_phase_Load` which tries to load
`/dvd/<name>.str` REL modules. We don't have REL modules — all code is in the
DOL. The canonical phase returns `cPhs_COMPLEATE_e` when load succeeds.

Identify where `fpcLd_Load` is in WW's binary and HLE-stub it to return
`cPhs_COMPLEATE_e` immediately. Currently the missing REL just propagates as a
no-find but the code path probably wastes cycles or hits error branches.

### 3. Correct `findVolume` / `getGlbResource` algorithm (medium payoff)

Match the canonical algorithm exactly:
- Path not starting with `/` → return `sCurrentVolume`.
- Parse volume name via `fetchVolumeName` (skip leading `/`, lowercase, stop at
  next `/`). Special-case `"/"` → volume `"/"`.
- Exact `strcmp` against `mVolumeName` (also lowercased at mount time).
- NULL on miss, no fallback.

Keep current fallback behavior available behind a debug flag — it likely masks
real bugs we want to see.

### 4. Use canonical struct layouts in `dolphin_dump.py` / dump parsing

Our captures dump magic field values without naming them. With
`base_process_class` documented (0xB8 bytes, fields at known offsets), we can
relabel the captures from "field+0x18" to `layer_tag.list_anchor`, etc. This
makes future captures and trace logs vastly more readable.

### 6. Wire game camera to renderer — INVESTIGATED, FOUND DIFFERENT ROOT CAUSE

**What we tried:** Instrument `GXLoadPosMtxImm`. Confirmed the camera
process dispatches but writes no GX matrices.

**Initial hypothesis (WRONG):** the Draw pass was bypassed. Suggested
wiring `fpcDw_Handler` as a multi-day foundational change.

**What we actually found:** The Draw pass is ALREADY wired in our main
loop (~line 2106). Confirmed via per-callback tracing in our
`0x80040198` HLE: every dispatched process receives both `[EXEC]
cb=0x8003E370` and `[DRAW] cb=0x8003E390` each frame. Function chain
`0x8003E390 → func_8003D51C (fpcDw_Execute) → func_8003D3C8 (fpcNd_Draw)
→ methods table draw_method` runs to completion every frame.

**Actual root cause:** The 9 dispatched processes are all **node**
processes (root/scene/room/camera/env/particles). Their `fpcNd_Draw`
recursively walks the sublayer's child list and there are no leaf
children. Leaf processes (actors/NPCs/doors/vegetation) are what
emit GX. They come from `room.dzr` ACTR/SCOB chunks → `fopAcM_create`.

**Implication:** the camera matrix never lands in GX until leaf
processes exist to be drawn. The "camera quick fix" path was never
real. Need actor spawning first.

**Function IDs confirmed:**
- `func_8003D51C = fpcDw_Execute`
- `func_8003D3C8 = fpcNd_Draw`
- `func_8003D7E0` = layer iterator wrapper
- `func_8024560C` = per-callback dispatcher
- `0x8003E370/E390` = execute/draw callback trampolines

**Diagnostic instrumentation added (leave in place):**
- `gcrecomp::gx::GXGetMatrixSlot` + `GXGetMatrixWriteCounter`
- `WW_USE_GAME_CAMERA=1` env var (inert without actors)
- `[EXEC]/[DRAW]` log distinguishes callback type per process

### 5. Wire the canonical priority/layer iteration

`fpcM_Management` (in `src/f_pc/f_pc_manager.cpp`) shows the *exact* per-frame
dispatch order:

```
MtxInit
dComIfGd_peekZdata (HIO)
fapGm_HIO_c::executeCaptureScreen
dShutdownErrorMsg_c::execute
dDvdErrorMsg_c::execute   (if not in DVD error)
  cAPIGph_Painter
  fpcDt_Handler
  fpcPi_Handler            <-- priority queue handler (move processes to new lists)
  fpcCt_Handler            <-- create request handler (run the 6-phase pipeline)
  fpcEx_Handler(fpcM_Execute)   <-- execute all processes in line iteration
  fpcDw_Handler(...)       <-- draw pass
dComIfGp_drawSimpleModel
```

Our `main.cpp` pumps create requests and runs `fpcEx_Handler`-equivalent
dispatch but in a different order. Aligning the order may surface or fix
ordering bugs.

## Things we should NOT lift directly

- **Profile IDs differ between TP and WW.** TP's `0x0015 = ENVSE`, WW's
  `0x0015 = scene_class`. The names in `f_pc_name.h` are conceptual reference,
  not transferable.
- **`fpcBs_MakeOfType` seed value.** TP starts at `0x9130000`. WW may differ —
  verify before assuming `0x09130001` is the first allocated type.
- **JSystem source.** Don't compile dusk's JSystem into ww — our recompiled
  PPC already contains WW's JSystem. Use the canonical source only as a
  reference for what our PPC code is supposed to do.

## Refresh

See `README.md` for the sparse-clone command.
