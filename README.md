# The Wind Waker - Static Recompilation

**A static recompilation of a GameCube game to native PC.**

This project takes *The Legend of Zelda: The Wind Waker* (GameCube, 2002) and
statically recompiles its PowerPC 750CXe (Gekko) machine code into native
x86-64 C code that runs directly on Windows 11. No emulator. No compatibility
layer. Just the Great Sea, running natively.

## What Is Static Recompilation?

Unlike an emulator that interprets or dynamically translates instructions at
runtime, static recompilation translates the *entire game binary* ahead of time
into compilable C source code. Each PowerPC instruction becomes a C expression:

```c
// Original GameCube PowerPC:
//   addi r3, r4, 0x20
//   lwz  r5, 0x10(r6)
//   bl   some_function

// Recompiled native C:
ctx->r[3] = (int32_t)ctx->r[4] + 0x20;
ctx->r[5] = MEM_READ32(ctx->r[6] + 0x10);
some_function(ctx, mem);
```

The generated C code compiles with any modern compiler (MSVC, Clang, GCC) and
runs at full native speed with all compiler optimizations applied.

## Architecture

```
                    +-----------------+
                    |   Wind Waker    |
                    |    DOL/REL      |
                    |  (PowerPC 750)  |
                    +--------+--------+
                             |
                    +--------v--------+
                    |  ww_recompiler  |
                    |  PPC Disasm ->  |
                    |  CFG Builder -> |
                    |  C Code Gen     |
                    +--------+--------+
                             |
                    +--------v--------+
                    | Recompiled .cpp |
                    | (x86-64 native) |
                    +--------+--------+
                             |
              +--------------+--------------+
              |              |              |
     +--------v------+ +----v----+ +-------v------+
     | GX Runtime    | | Audio   | | Input        |
     | (D3D11)       | | (DSP    | | (XInput /    |
     | TEV -> HLSL   | |  ADPCM) | |  Keyboard)   |
     | Texture decode| | XAudio2 | | GC Pad map   |
     +---------------+ +---------+ +--------------+
```

### Components

| Component | Description | Status |
|-----------|-------------|--------|
| **DOL Parser** | Parses GameCube executable format (7 text + 11 data sections) | Done |
| **REL Parser** | Parses relocatable modules (actors, scenes) | Done |
| **PPC Disassembler** | Decodes all Gekko instructions including Paired Singles | Done |
| **CFG Builder** | Identifies functions and basic blocks from binary | Done |
| **PPC-to-C Emitter** | Translates each PPC instruction to C code | Done |
| **Runtime** | CPU context (GPR/FPR/PS/CR/SPR), memory, function dispatch | Done |
| **Function Table** | Auto-generated registration of all 8,148 functions for indirect dispatch | Done |
| **GX Graphics** | GameCube GX API -> Direct3D 11 translation | In Progress |
| **TEV Shader Gen** | Generates HLSL shaders from TEV stage configurations | In Progress |
| **Texture Decoder** | Decodes GC texture formats (I4/I8/IA/RGB565/RGB5A3/CMPR) | Done |
| **Audio** | DSP ADPCM decoder, voice mixer | In Progress |
| **Input** | Keyboard/XInput -> GameCube pad mapping | Done |
| **OS Functions** | Replacement implementations for Dolphin OS calls (heap, DVD, VI, CARD, threads) | In Progress |

### Supported Instructions

The disassembler and recompiler handle the full Gekko instruction set:

- **Integer**: ADD, SUB, MUL, DIV, CMP, AND, OR, XOR, shift, rotate
- **Floating Point**: Single and double precision arithmetic, compare, convert
- **Paired Singles**: The Gekko's SIMD extension — PS_ADD, PS_MUL, PS_MADD, PS_MERGE, quantized load/store, etc.
- **Load/Store**: All sizes (byte/half/word/float/double), indexed, update, multiple, byte-reversed
- **Branch**: Direct, conditional, to LR/CTR, with/without link
- **System**: SPR access (LR, CTR, XER, GQR), CR manipulation, cache ops, sync

## Requirements

- **Game**: A legally obtained copy of *The Wind Waker* (GZLE01 - US version)
- **Windows 11** with a DirectX 11 compatible GPU
- **CMake** 3.20+
- **Visual Studio 2022** or compatible C++20 compiler

## Building

```bash
# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build the recompiler tool
cmake --build build --target ww_recompiler --config Release

# Recompile the game (you need the DOL extracted from your ISO)
./build/Release/ww_recompiler.exe main.dol --extra-funcs extra_funcs.txt --output recompiled/

# IMPORTANT: After recompiling, apply patches to recompiled source:
#   See "Recompiled Source Patches" section below for the full list.
#   At minimum: recomp_0018.cpp (display init), recomp_0063.cpp (bump alloc),
#   recomp_0066.cpp (VI wait, assert), recomp_0006.cpp (GX sync),
#   recomp_0000.cpp (exception handler), recomp_0067.cpp (camera GX),
#   recomp_0075.cpp (DVD read), recomp_0074.cpp (async DVD),
#   recomp_0080.cpp (bctr tail call), recomp_0001.cpp (heap),
#   recomp_0064.cpp (JKR resource lookup)

# Build the game runtime + recompiled code
cmake --build build --target ww_launcher --config Release

# Run (place your GZLE01 ISO as ww.iso in the project directory)
./build/Release/ww_launcher.exe
# Or specify paths explicitly:
./build/Release/ww_launcher.exe main.dol --iso /path/to/game.iso
```

## Controls

| Input | Action |
|-------|--------|
| WASD | Move Link |
| Mouse / Arrow Keys | Camera |
| Space | A — Roll, Talk, Grab |
| Left Shift | B — Sword |
| E | X — Item Slot 1 |
| Q | Y — Item Slot 2 |
| R | Z — Target Lock |
| Tab | R — Shield |
| F | L Trigger |
| Enter | Start / Pause |
| ESC | Quit |

XInput gamepads are also supported with standard button mapping.

## How It Works

### 1. Parse the DOL

The GameCube executable (DOL) contains up to 7 code sections and 11 data
sections, all big-endian. We parse these and build a flat memory image.

### 2. Disassemble

Every 4-byte word in code sections is decoded into a structured instruction.
The Gekko is a PowerPC 750CXe with ~200 base instructions plus ~50 Paired
Singles SIMD extensions.

### 3. Build Control Flow Graph

We identify function boundaries by scanning for `bl` (branch-and-link)
instructions, then trace execution flow to find basic blocks. Each function
becomes a C function.

### 4. Emit C Code

Each instruction becomes a C expression operating on a `PPCContext` struct
that mirrors the Gekko's register file. Memory accesses go through
big-endian read/write helpers.

### 5. Replace OS & Hardware

GameCube OS calls (memory allocation, DVD access, timing) are intercepted
and replaced with native implementations. GX graphics calls are translated
to Direct3D 11. Audio is decoded from DSP ADPCM and played via XAudio2.

## Project Status

The recompiler can process the full Wind Waker DOL, generate C code for all
discovered functions, and the runtime provides the hardware abstraction
needed to run it. The game renders textured 3D scene geometry from actual
disc data through a GX→D3D11 translation pipeline at 60fps.

### Recompiler Statistics

| Metric | Value |
|--------|-------|
| DOL Size | 4.16 MB |
| Entry Point | 0x80003140 |
| Functions Discovered | 8,148 |
| Total Instructions | 348,958 |
| Instruction Coverage | 99.99% (1 unknown encoding) |
| Output Files | 41 translation units + registration table |
| Supported Instruction Classes | Integer, Float, Paired Singles, Load/Store, Branch, CR, System |

The recompiler achieves near-perfect instruction coverage across the full
Gekko ISA, including all Paired Singles SIMD instructions and quantized
load/store operations with GQR-based dequantization.

### Runtime Status

The game is **rendering textured 3D geometry from actual game data at 60fps**:

- All 41 translation units compile with MSVC (C++20)
- DOL loaded, all 100 static constructors complete
- Game framework (`fapGm_Create`) initialized successfully
- Main game loop running at ~60 FPS with full `fapGm_Execute` dispatch
- **J3D/BDL model parser** — extracts geometry, textures, and materials from Nintendo's binary model format
- **Indexed triangle rendering** — SHP1 display lists parsed, triangle strips/fans converted, vertex indices resolved
- **CMPR texture decoding** — 17 textures decoded (DXT1 variant) and bound via GX pipeline
- **Per-batch material binding** — INF1 scene graph maps shapes to materials to textures
- **Multi-model scene** — island terrain (5,946 verts, 8 batches) + ocean water (1,194 verts, 8 batches)
- **TEV shader pipeline** — texture * vertex color modulation, compiled and cached per-state
- **Full D3D11 draw path** — vertex buffer upload, pixel shader, blending, depth test, present
- **Orbiting camera** — perspective projection with automatic rotation around the island
- **JKR archive mount system** — proper JKRMemArchive objects in emulated memory with working vtable, linked into sVolumeList
- Stage.arc Yaz0 decompressed (59KB → 299KB), Room44.arc loaded (714KB), stage.dzs parsed
- 16 hardware-dependent functions patched in recompiled source

### Loaded Archives (sea_T — Great Sea)

**Stage.arc** (59KB Yaz0 → 299KB decompressed):
```
tex/cloudtx_01.bti   4,128 bytes   Cloud texture
tex/cloudtx_02.bti   4,128 bytes   Cloud texture
tex/cloudtx_03.bti   4,128 bytes   Cloud texture
dzs/stage.dzs         4,608 bytes   Stage data (13 chunks)
dat/event_list.dat  227,804 bytes   Event scripts
bdl/vr_back_cloud.bdl 40,096 bytes  Skybox cloud model
bdl/vr_kasumi_mae.bdl  3,808 bytes  Skybox haze model
bdl/vr_sky.bdl         6,496 bytes  Skybox model
bdl/vr_uso_umi.bdl     3,072 bytes  Sea surface model
```

**Room44.arc** (714KB raw RARC):
```
dzr/room.dzr          10,880 bytes  Room data/actors
dzb/room.dzb         121,504 bytes  Room collision mesh
dat/m128.amp          35,456 bytes  Lightmap
dat/m128.bti          21,568 bytes  Lightmap texture
dat/s128.bti           7,264 bytes  Shadow texture
btk/model1.btk         2,784 bytes  Texture animation
bdl/model.bdl        399,872 bytes  Main room model (390KB)
bdl/model1.bdl       111,296 bytes  Secondary room model
bdl/model3.bdl         3,360 bytes  Tertiary room model
```

**stage.dzs Chunk Table** (13 chunks):
```
STAG (1)    Stage settings          RTBL (50)   Room table
EVNT (1)    Event data              MULT (2)    Spawn multipliers
EnvR (50)   Environment registers   Colo (5)    Color settings
Pale (33)   Palette entries         Virt (31)   Virtual light settings
RPAT (4)    Room paths              RPPN (30)   Path points
ACTR (2)    Actor placements        RCAM (1)    Camera
RARO (1)    RARC override
```

### What Works

| System | Status |
|--------|--------|
| Static constructors (100/100) | Working |
| Game framework (fapGm) | Working — full layer dispatch every frame |
| DVD/ISO file access | Working — FST parsed, both archives loaded from disc |
| Yaz0 decompression | Working — 59KB → 299KB decompressed correctly |
| RARC archive parsing | Working — 18 files across 2 archives |
| J3D/BDL model parsing | Working — VTX1, SHP1, INF1, TEX1 sections extracted |
| Indexed geometry rendering | Working — display list parsing, strip/fan→triangle conversion |
| CMPR texture decoding | Working — 17 textures decoded and bound to GX pipeline |
| Per-batch materials | Working — INF1 scene graph maps shapes to textures |
| TEV shader generation | Working — HLSL shaders compiled and cached per-state |
| D3D11 draw pipeline | Working — vertex buffers, pixel shaders, blending, depth |
| Multi-model scene | Working — terrain (opaque) + water (translucent alpha blend) |
| DZS stage data parsing | Working — 13 chunks, full chunk table decoded |
| Memory allocation | Working — bump allocator replaces JKR heap |
| JKR archive mounting | Working — JKRMemArchive objects in emulated memory, sVolumeList linked |
| getGlbResource / findVolume | Working — searches mounted archives, returns real JKR objects |
| Time Base (TB) register | Working — host QueryPerformanceCounter scaled to 40.5MHz |
| VI (Video Interface) HLE | Working — VIInit, VIWaitForRetrace (60Hz), VIFlush, framebuffer |
| CARD (Memory Card) HLE | Working — host FS backed (memcard_a/, memcard_b/), full read/write |
| Init stubs (OSInit, DVDInit, etc.) | Working — boot-time SDK calls satisfied |
| PPC helpers (ppc_helpers.h) | Working — CNTLZW, ROTL32, MFTBL/U, PSQ load/store from gcrecomp |
| HW register HLE | Working — VI line counter, SI/DI status, rate-limited logging |

### Framework Process System

The game's multi-threaded framework (fopMsgM/fapGm) is now operational:

- **9 processes dispatching every frame** — root layer (0x0007), scene (0x0015),
  room manager (0x0017), environment (0x0028), camera (0x00A9), 3 scene sub-processes
  (0x01B5/0x01BA/0x01BB), and timing controller (0x01BC)
- **Both execute and draw phases run** — fn_table[2] and fn_table[3] dispatched via
  3-level callback chain through the per-frame layer iteration
- **Frame gate bypassed** — VRetrace check always returns 0 without interrupts;
  we call dispatch sub-functions directly, skipping the gate
- **Execution queue populated** — process nodes at +0x34 linked into 0x803BCD60,
  with 16 priority lists iterated by func_802450D0
- **Dolphin reference state loaded** — 1170 global values from game info/SDA globals
  plus 831 scalar template values across 8 process types captured during Dolphin gameplay
- Scene state = 0 (active), matching Dolphin runtime

Process execute methods run but don't yet modify game state because process objects
lack internal pointer state (set during construction, which is stubbed due to JKR
archive dependencies).

### Current Focus

Two systems are end-to-end functional:

1. **Rendering**: ISO → Yaz0 → RARC → BDL → J3D parse → GX → D3D11 → 60fps
2. **Framework dispatch**: fapGm per-frame → layer iteration → per-process execute+draw

The main blocker is the **process creation chain**: the original create functions
(e.g., func_80022CEC) depend on JKR archive mounting, which requires a background
thread and properly initialized heap system. Process objects are constructed with
correct headers and scalar state from Dolphin captures, but lack the internal
pointer state that the real create functions would initialize.

### Next Steps

- **Implement JKR archive mounting** — the main blocker for the entire process creation
  chain. Every process create function (actors, camera, environment) loads resources
  from JKR archives during construction. Without a working JKRMemArchive mount system,
  processes can be allocated but not properly initialized.
- **Get past the title screen** — requires enough of the framework (scene transitions,
  input dispatch, UI rendering) to handle the title/file-select flow
- **Connect game camera** — the camera process has 610 values from Dolphin including
  position/FOV; use these to replace the hardcoded orbiting camera
- **Actor spawning** — parse room.dzr actor placements and create actor processes
- **MAT3 material parsing** — full TEV stage configuration from J3D materials

## Standing on the Shoulders of Giants

This project builds on incredible work by the community:

- [**zeldaret/tww**](https://github.com/zeldaret/tww) — Wind Waker decompilation project providing invaluable symbol maps and architectural understanding
- [**zeldaret/tp**](https://github.com/zeldaret/tp) — Twilight Princess decompilation; the shared Nintendo "ZeldaFramework" code (`f_pc_*`, `f_op_*`, `f_ap_*`) is nearly verbatim with WW and is our canonical reference for framework struct layouts
- [**TwilitRealm/dusk**](https://github.com/TwilitRealm/dusk) (Dusklight) — TP source port whose decomp headers we vendor under `docs/dusk-ref/` (CC0) as a reverse-engineering reference for the framework we recompile
- [**Aurora**](https://github.com/encounter/aurora) — Source-level GC/Wii compatibility layer (different translation strategy than ours, but a model for cross-platform GX/DVD/PAD host services)
- [**N64Recomp**](https://github.com/N64Recomp/N64Recomp) — Pioneered the static recompilation approach for N64 games
- [**Dolphin**](https://github.com/dolphin-emu/dolphin) — The gold standard GameCube/Wii emulator, whose GX shader generation code informs our TEV implementation
- [**decomp-toolkit**](https://github.com/encounter/decomp-toolkit) — GameCube binary analysis tools
- [**powerpc-rs**](https://github.com/encounter/powerpc-rs) — Fuzz-tested PPC750 disassembler reference

## License

**MIT.** See [LICENSE](LICENSE). The recompiler and runtime library it builds on,
[gcrecomp](https://github.com/sp00nznet/gcrecomp), is MIT as well.

That is deliberate, and it is the point of the project. Every other GameCube
static recompilation stack is a Dolphin derivative and therefore GPL:
[DolRecomp](https://github.com/ExpansionPak/DolRecomp) and
[ModernGekko](https://github.com/ExpansionPak/ModernGekko) are GPLv3, and everything
built on them inherits that. Writing our own GX, OS, and hardware layers from scratch
is slower than vendoring an emulator, and it is the only way to end up with a
permissively licensed toolchain — one that can be embedded in a commercial port,
shipped on a console, or used by a preservation project that cannot take GPL.

Keeping that claim defensible means treating other emulators as black boxes: we
observe behaviour (memory dumps, traces, framebuffer output) and never read an
implementation and reimplement from it.

Third-party material vendored here: `docs/dusk-ref/` carries decomp headers from
[TwilitRealm/dusk](https://github.com/TwilitRealm/dusk) under CC0.

## Legal

This project does not include any copyrighted game assets or code. You must
provide your own legally obtained copy of *The Legend of Zelda: The Wind Waker*.

## Part of sp00nznet

Built with the same static recompilation methodology as our other projects.
The Great Sea has never been closer.

---

*"The wind... it is blowing."* — The King of Red Lions
