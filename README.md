# The Wind Waker - Static Recompilation

```
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      _____ _          __      ___           _
     |_   _| |_  ___  \ \    / (_)_ _  __| |
       | | | ' \/ -_)  \ \/\/ /| | ' \/ _` |
       |_| |_||_\___|  _\_/\_/ |_|_||_\__,_|
         \ \    / /_ _| |_____ _ _
          \ \/\/ / _` | / / -_) '_|
           \_/\_/\__,_|_\_\___|_|

       No emulator. Just the wind at your back.
  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

**The first-ever static recompilation of a GameCube game.**

This project takes *The Legend of Zelda: The Wind Waker* (GameCube, 2002) and
statically recompiles its PowerPC 750CXe (Gekko) machine code into native
x86-64 C code that runs directly on Windows 11. No emulator. No compatibility
layer. Just Link, rolling around the Great Sea, at native speed.

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
| **OS Functions** | Replacement implementations for Dolphin OS calls | In Progress |

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

# IMPORTANT: After recompiling, patch recomp_0018.cpp:
#   In func_800AD17C, comment out the calls to func_8025214C and func_80252020
#   (these contain VI register busy-waits that hang without HW emulation)

# Build the game runtime + recompiled code
cmake --build build --target ww_launcher --config Release

# Run
./build/Release/ww_launcher.exe
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

This is the **first-ever GameCube static recompilation project** to reach
the pipeline-complete stage. The recompiler can process the full Wind Waker
DOL, generate C code for all discovered functions, and the runtime provides
the hardware abstraction needed to run it.

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

The game is **running**:

- All 41 translation units compile with MSVC (C++20)
- DOL loaded, all 100 static constructors complete
- Game framework (`fapGm_Create`) initialized successfully
- Main game loop running at ~60 FPS with native fapGm layer dispatch
- VRetrace timing gate bypassed (no HW interrupt emulation needed)
- Display init busy-waits (VI register polling) stubbed out
- PPCHalt infinite spin replaced with cooperative yield
- Only 3 unresolved indirect calls remain (data/BSS section addresses — not recompilable)

### Current Focus

- GX TEV stage -> HLSL shader pipeline (getting pixels on screen)
- Display list processing
- REL (actor/scene module) loading and relocation
- Expanding OS HLE coverage (thread scheduling, DVD file access)
- Resolving remaining indirect call targets

## Standing on the Shoulders of Giants

This project builds on incredible work by the community:

- [**zeldaret/tww**](https://github.com/zeldaret/tww) — Wind Waker decompilation project providing invaluable symbol maps and architectural understanding
- [**N64Recomp**](https://github.com/N64Recomp/N64Recomp) — Pioneered the static recompilation approach for N64 games
- [**Dolphin**](https://github.com/dolphin-emu/dolphin) — The gold standard GameCube/Wii emulator, whose GX shader generation code informs our TEV implementation
- [**decomp-toolkit**](https://github.com/encounter/decomp-toolkit) — GameCube binary analysis tools
- [**powerpc-rs**](https://github.com/encounter/powerpc-rs) — Fuzz-tested PPC750 disassembler reference

## Legal

This project does not include any copyrighted game assets or code. You must
provide your own legally obtained copy of *The Legend of Zelda: The Wind Waker*.

## Part of sp00nznet

Built with the same static recompilation methodology as our other projects.
The Great Sea has never been closer.

---

*"The wind... it is blowing."* — The King of Red Lions
