// =============================================================================
// Wind Waker Static Recompilation - Main Launcher
//
// "The wind... it is blowing."
//
// This is the entry point for the recompiled game.
// It initializes all subsystems, loads the game data, and enters the
// main loop. Link's adventure on the Great Sea, running natively on
// Windows 11 — no emulator in sight.
// =============================================================================

#include "ww/runtime.h"
#include "ww/dol.h"
#include "ww/gx/gx.h"
#include "ww/audio/audio.h"
#include "ww/input/input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>

// Auto-generated function registration (from recompiler output)
extern void register_recompiled_functions(ww::FuncTable& table);

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace ww;

static const uint32_t WINDOW_WIDTH  = 1280;
static const uint32_t WINDOW_HEIGHT = 720;

static std::atomic<bool> g_game_running{false};

// Forward declarations for recompiled functions
extern void func_8030CFB0(PPCContext* ctx, Memory* mem);  // Small init helper
extern void func_80309A68(PPCContext* ctx, Memory* mem);  // __init_user (static ctors)
extern void func_80006464(PPCContext* ctx, Memory* mem);  // main()

// From main01__Fv (0x80006338) — the REAL game loop:
extern void func_80006338(PPCContext* ctx, Memory* mem);  // main01 (init + game loop)
extern void func_8000C70C(PPCContext* ctx, Memory* mem);  // mDoCPd_Create (controller init)
extern void func_8000BC94(PPCContext* ctx, Memory* mem);  // mDoGph_Create (graphics init)
extern void func_80007A70(PPCContext* ctx, Memory* mem);  // mDoRst_Create (reset init)
extern void func_80023218(PPCContext* ctx, Memory* mem);  // fapGm_Create (framework create)
extern void func_80022DF8(PPCContext* ctx, Memory* mem);  // framework post-create init

// Per-frame functions from main01's loop:
extern void func_800078C0(PPCContext* ctx, Memory* mem);  // mDoRst_Execute (reset check)
extern void func_80007224(PPCContext* ctx, Memory* mem);  // mDoAud_Execute (audio)
extern void func_800231E4(PPCContext* ctx, Memory* mem);  // fapGm_Execute (framework execute!)
extern void func_80006264(PPCContext* ctx, Memory* mem);  // main loop cleanup
extern void func_8003EC84(PPCContext* ctx, Memory* mem);  // fapGm layer dispatch
extern void func_802539A4(PPCContext* ctx, Memory* mem);  // fapGm sub-call 1
extern void func_8003EBD4(PPCContext* ctx, Memory* mem);  // fapGm sub-call 2
extern void func_8024135C(PPCContext* ctx, Memory* mem);
extern void func_8003D314(PPCContext* ctx, Memory* mem);
extern void func_8003FF00(PPCContext* ctx, Memory* mem);
extern void func_8003D150(PPCContext* ctx, Memory* mem);
extern void func_8003D7E0(PPCContext* ctx, Memory* mem);
extern void func_800404CC(PPCContext* ctx, Memory* mem);
extern void func_802449AC(PPCContext* ctx, Memory* mem);  // fapGm counter update

// Addresses of constructors known to hang (infinite loops or HW dependencies)
static const uint32_t SKIP_CTORS[] = {
    // 0x800559E8 — was skipped, now traced via instrumented recomp_0009.cpp
    0,
};

static bool should_skip_ctor(uint32_t addr) {
    for (int i = 0; SKIP_CTORS[i]; i++) {
        if (SKIP_CTORS[i] == addr) return true;
    }
    return false;
}

// Replacement for func_80309A88 (__init_user loop) — traces each static constructor
static void traced_init_user(PPCContext* ctx, Memory* mem) {
    // The constructor table starts at 0x80338680 (data2 section)
    uint32_t table_addr = 0x80338680;
    int count = 0, skipped = 0;
    while (true) {
        uint32_t ctor_addr = mem->read32(table_addr);
        if (ctor_addr == 0) break;
        count++;
        if (should_skip_ctor(ctor_addr)) {
            skipped++;
        } else {
            g_func_table.call(ctor_addr, ctx, mem);
        }
        table_addr += 4;
    }
    printf("[*] %d static constructors processed (%d skipped)\n", count, skipped);
}

// Replacement for func_8003EBD4 (frame timing gate)
// The original calls func_80312300 (VRetrace check) which reads interrupt-driven
// VBlank state. Without interrupt emulation, it always returns 0 = "no frame".
// The logic then clears the frame-ready counter and returns 0, preventing any
// game processing. We bypass all of that and always return 1 = "process frame".
static void frame_gate_replacement(PPCContext* ctx, Memory* mem) {
    ctx->r[3] = 1;  // Always signal "frame ready"
}

// Replacement for func_8030150C (PPCHalt / idle loop)
// The original is an infinite spin loop. We replace it with a sleep+return
// so the game thread can yield to the host and the main loop can pump messages.
static void idle_loop_replacement(PPCContext* ctx, Memory* mem) {
    // Just return immediately — the game main will call us again next frame
    // via func_80309ADC. This simulates one "frame" of work.
    ctx->r[3] = 0;
}

// Load DOL sections into emulated memory
static bool load_dol_into_memory(const char* path, Memory& mem) {
    ww::DOLFile dol;
    if (!dol.load(path)) {
        fprintf(stderr, "[DOL] Failed to load: %s\n", path);
        return false;
    }
    dol.print_info();

    // Copy each section's data into emulated RAM
    // DOL data is already big-endian, and Memory stores big-endian bytes
    for (const auto& sec : dol.sections) {
        if (sec.size == 0) continue;
        uint8_t* dst = mem.translate(sec.address);
        if (dst == mem.ram && sec.address != Memory::MAIN_RAM_BASE) {
            fprintf(stderr, "[DOL] Section at 0x%08X is out of range, skipping\n", sec.address);
            continue;
        }
        memcpy(dst, sec.data.data(), sec.size);
        printf("[DOL] Loaded %s%d: 0x%08X - 0x%08X (%u bytes)\n",
               sec.is_text ? "text" : "data", sec.index,
               sec.address, sec.address + sec.size, sec.size);
    }

    // Zero BSS
    if (dol.bss_size > 0) {
        uint8_t* bss = mem.translate(dol.bss_address);
        memset(bss, 0, dol.bss_size);
        printf("[DOL] Zeroed BSS: 0x%08X - 0x%08X (%u bytes)\n",
               dol.bss_address, dol.bss_address + dol.bss_size, dol.bss_size);
    }

    printf("[DOL] All sections loaded.\n");
    return true;
}

static void print_banner() {
    printf("\n");
    printf("  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("      _____ _          __      ___           _\n");
    printf("     |_   _| |_  ___  \\ \\    / (_)_ _  __| |\n");
    printf("       | | | ' \\/ -_)  \\ \\/\\/ /| | ' \\/ _` |\n");
    printf("       |_| |_||_\\___|  _\\_/\\_/ |_|_||_\\__,_|\n");
    printf("         \\ \\    / /_ _| |_____ _ _\n");
    printf("          \\ \\/\\/ / _` | / / -_) '_|\n");
    printf("           \\_/\\_/\\__,_|_\\_\\___|_|\n");
    printf("\n");
    printf("       Static Recompilation - Native Windows 11\n");
    printf("       GameCube PowerPC 750 (Gekko) -> x86-64\n");
    printf("       No emulator. Just the wind at your back.\n");
    printf("  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("\n");
}

int main(int argc, char** argv) {
    print_banner();

    // ---- Determine DOL path ----
    const char* dol_path = "main.dol";
    if (argc >= 2) dol_path = argv[1];

    // ---- Initialize Runtime ----
    printf("[*] Initializing runtime...\n");
    if (!runtime_init()) {
        fprintf(stderr, "Failed to initialize runtime\n");
        return 1;
    }

    // ---- Load DOL into emulated memory ----
    printf("[*] Loading DOL: %s\n", dol_path);
    if (!load_dol_into_memory(dol_path, g_mem)) {
        fprintf(stderr, "Failed to load DOL\n");
        runtime_shutdown();
        return 1;
    }

    // ---- Initialize OS low-memory and HLE ----
    init_low_memory(&g_mem);
    register_os_functions();

    // ---- Set CPU registers from __init_registers (func_80003278) ----
    g_ctx.r[1]  = 0x8040CFA8;  // Stack pointer
    g_ctx.r[2]  = 0x803FFD00;  // SDA2 base (_SDA2_BASE_)
    g_ctx.r[13] = 0x803FE0E0;  // SDA base (_SDA_BASE_)

    // ---- Register Recompiled Functions ----
    printf("[*] Registering recompiled functions...\n");
    register_recompiled_functions(g_func_table);

    // ---- Initialize Graphics ----
    printf("[*] Initializing graphics (D3D11)...\n");
    gx::GXInit();
    if (!gx::GXInitBackend(nullptr, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        fprintf(stderr, "Failed to initialize D3D11 backend\n");
        runtime_shutdown();
        return 1;
    }

    // ---- Initialize Audio ----
    printf("[*] Initializing audio...\n");
    audio::audio_init(32000, 2);

    // ---- Initialize Input ----
    printf("[*] Initializing input...\n");
    input::input_init();

    // ---- Run game initialization ----
    printf("[*] Running game init...\n");

    // Init helper (sets up SDA-relative globals)
    func_8030CFB0(&g_ctx, &g_mem);
    printf("[*] Init helper done.\n");

    // Note: func_8030173C (DBInit/OSInit) skipped — it does hardware register
    // init (writing to 0xCC000000+) that hangs without full HW emulation.

    // Static constructors — use traced replacement to identify hangs
    printf("[*] Running static constructors (__init_user)...\n");
    fflush(stdout);
    traced_init_user(&g_ctx, &g_mem);
    printf("[*] Static constructors complete.\n");
    fflush(stdout);

    // ---- Set up game's bump allocator heap ----
    printf("[*] Initializing game heap pointer...\n");
    {
        uint32_t heap_ptr_addr = 0x803F66C0;  // r13(-31264)
        uint32_t arena_lo = 0x80400000;
        g_mem.write32(heap_ptr_addr, arena_lo);
        printf("[*] Heap pointer at 0x%08X = 0x%08X\n", heap_ptr_addr, arena_lo);
    }

    // ---- Override problematic functions ----
    // PPCHalt (0x8030150C): infinite spin loop → return immediately
    g_func_table.register_func(0x8030150C, idle_loop_replacement);
    // Frame timing gate: always return "process frame"
    g_func_table.register_func(0x8003EBD4, frame_gate_replacement);

    // ---- Initialize game framework (from main01__Fv = func_80006338) ----
    // main01 does: mDoCPd_Create, mDoGph_Create, mDoRst_Create, fapGm_Create,
    // then enters an infinite loop calling fapGm_Execute each frame.
    // We skip hardware-dependent init (controller, graphics mode) since we
    // handle those via host APIs, and run the game framework creation.
    printf("[*] Initializing game framework...\n");
    fflush(stdout);

    // Skip mDoCPd_Create (0x8000C70C) — hangs on PAD/SI hardware init
    // Skip mDoGph_Create (0x8000BC94) — tries to init GX via hardware
    // Skip mDoRst_Create (0x80007A70) — reset controller, might need HW

    printf("[*]   fapGm_Create (game framework)...\n"); fflush(stdout);
    func_80023218(&g_ctx, &g_mem);
    printf("[*]   fapGm_Create done.\n"); fflush(stdout);

    printf("[*]   Framework post-init (func_80022DF8)...\n"); fflush(stdout);
    func_80022DF8(&g_ctx, &g_mem);
    printf("[*] Game framework initialized.\n");
    fflush(stdout);

    // ---- Launch Game Thread ----
    printf("[*] Launching game thread (main01 loop)...\n");
    printf("[*] (Press ESC to quit)\n\n");

    g_game_running = true;
    std::thread game_thread([&]() {
        fprintf(stderr, "[*] Entering main game loop (fapGm_Execute per frame)...\n");

        int frame = 0;
        while (g_game_running) {
            // Per-frame work from main01's loop:
            // 1. func_800078C0 — mDoRst_Execute (reset/restart check)
            // 2. func_80007224 — mDoAud_Execute (audio processing)
            // 3. func_800231E4 — fapGm_Execute (game framework: actors, scenes!)
            // 4. func_80006264 — main loop cleanup
            func_800078C0(&g_ctx, &g_mem);  // mDoRst_Execute
            func_80007224(&g_ctx, &g_mem);  // mDoAud_Execute

            // Native fapGm_Execute — bypass VRetrace gate
            // func_8003EBD4 checks VRetrace interrupt state which is never set
            // without hardware interrupt emulation. We call the layer dispatch
            // sub-functions directly, forcing frame processing every iteration.
            {
                func_802539A4(&g_ctx, &g_mem);
                func_8024135C(&g_ctx, &g_mem);
                func_8003D314(&g_ctx, &g_mem);
                func_8003FF00(&g_ctx, &g_mem);
                func_8003D150(&g_ctx, &g_mem);
                g_ctx.r[3] = 0x8003E370;
                func_8003D7E0(&g_ctx, &g_mem);
                // Skip func_800404CC (GX sync callback) — hangs without HW
            }
            func_802449AC(&g_ctx, &g_mem);  // fapGm counter update

            func_80006264(&g_ctx, &g_mem);  // main loop cleanup

            frame++;
            if (frame <= 5 || frame % 60 == 0) {
                fprintf(stderr, "[*] Frame %d complete\n", frame);
            }

            Sleep(16);  // ~60 FPS
        }

        fprintf(stderr, "[*] Game thread exiting after %d frames.\n", frame);
    });
    game_thread.detach();

#ifdef _WIN32
    // Main thread: Windows message pump
    bool running = true;
    while (running && g_game_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        input::input_update();
        audio::audio_update();
        gx::GXPresent();
        Sleep(16);  // ~60 FPS cap
    }
#endif

    // ---- Cleanup ----
    printf("\n[*] Shutting down...\n");
    input::input_shutdown();
    audio::audio_shutdown();
    gx::GXShutdownBackend();
    runtime_shutdown();

    printf("[*] Thanks for sailing the Great Sea! See you next time.\n");
    return 0;
}
