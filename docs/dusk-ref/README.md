# dusk reference headers

This directory contains a vendored, read-only snapshot of headers from the
[TwilitRealm/dusk](https://github.com/TwilitRealm/dusk) project (Dusklight, a
Twilight Princess source port built on the [zeldaret/tp](https://github.com/zeldaret/tp)
decompilation).

**Nothing in this directory is compiled.** It exists purely as a reverse-engineering
reference. TP and WW share Nintendo's "ZeldaFramework" code (`f_pc_*`, `f_op_*`,
`f_ap_*`) nearly verbatim, so dusk's decomp serves as the canonical layout for
the same structs we're reconstructing from PPC in WW.

## What's here

- `include/f_pc/` — process framework headers: layers, nodes, priority queues,
  create requests, executors. The framework dispatch system we currently
  reconstruct by offset (`0x803726A0`, sublayers at `0x803BCE20`, etc.).
- `include/f_op/` — scene/actor/overlap manager headers. `f_op_scene_req.h` /
  `f_op_scene_mng.h` map directly to `func_8000AC3C` / `func_8000AF2C`.
- `include/f_ap/` — top-level `fapGm_*` application layer header (matches our
  `0x80371D10` global and `fapGm_Execute`).
- `src/f_pc/` — framework algorithm sources (32 files). `fpcBs_Create`,
  `fpcSCtRq_*` phases, `fpcNdRq_Request` (scene change pipeline), `fpcM_Management`
  (per-frame dispatch order).
- `src/f_op/` — scene/actor/overlap source (23 files). Phase handlers for
  scene fade, overlap, change.
- `src/f_ap/` — `fapGm_*` application top-level source.
- `jsystem/JKernel/` — JKR (Nintendo memory/archive system) headers and source
  for `JKRFileLoader`, `JKRMemArchive`, `JKRArchive` (pri+pub), `JKRDvdRipper`,
  `JKRDvdFile`, `JKRHeap`, etc. Direct reference for our HLE patches
  `func_802B6FEC` (getGlbResource) and `func_802B6AB8` (findVolume).
- `scene/d_stage.cpp` + `d_stage.h` — stage data loading/parsing reference.

See `audit.md` for the concrete deltas between our HLE patches and the canonical
sources, with a fix-list.

## Confirmed corrections to our reverse engineering

- `base_process_class` is **0xB8 bytes**, with `type@0x00`, `id@0x04`,
  `profname@0x0E`, `profile@0x10`, `layer_tag@0x18`, `priority@0x68`,
  `methods@0xA8`. Matches what we've been reading by offset.
- The "magic" `0x09130001` at process+0x00 is `g_fpcBs_type` from
  `fpcBs_MakeOfType` (seeded at `0x9130000`, incremented per call).
- Layers have **N node lists** (variable, passed to `fpcLy_Create`). Root layer
  uses 10; node processes (sublayers) use 16. Our previous count of "8 priority
  lists per sublayer" was wrong — the sparse offsets we sampled were a subset
  of 16 uniform 12-byte slots.
- The init_state values we observed (0→1→2→3) are: 0=created, 1=loading,
  2=executing, 3=delete-queued.
- Scene phase return values (`cPhs_INIT_e / NEXT_e / COMPLEATE_e / UNK3_e / ERROR_e`)
  drive the state machine we traced.

## Credit

Dusklight is the work of the TwilitRealm team, building on the zeldaret/tp
decompilation. Their project page: <https://twilitrealm.dev>. The dusk repo is
licensed CC0 1.0 (`LICENSE.md` in this directory).

We are not affiliated with dusk and our project is a different translation
strategy (static recompilation of the WW DOL, not a decomp-based port). Their
headers are used here only as a reverse-engineering reference for shared
Nintendo framework code.

## How to refresh

```sh
git clone --depth 1 --filter=blob:none --sparse https://github.com/TwilitRealm/dusk.git /tmp/dusk-ref
cd /tmp/dusk-ref
git sparse-checkout set --skip-checks include/f_pc include/f_op include/f_ap LICENSE.md
cp -r include/{f_pc,f_op,f_ap} <ww>/docs/dusk-ref/include/
cp LICENSE.md <ww>/docs/dusk-ref/LICENSE.md
```
