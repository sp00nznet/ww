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
#include "ww/j3d.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <cmath>

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

// Parsed BDL model for rendering (room geometry)
static j3d::J3DModel g_room_model;
static const uint8_t* g_room_bdl_data = nullptr;
static uint32_t g_room_bdl_size = 0;
static bool g_room_model_loaded = false;
static bool g_textures_loaded = false;
static float g_camera_angle = 0.0f;
static gcrecomp::gx::GXTexObj g_tex_objs[8] = {}; // up to 8 textures

// Forward declarations for recompiled functions
extern void func_8030CFB0(PPCContext* ctx, Memory* mem);  // Small init helper
extern void func_80309A68(PPCContext* ctx, Memory* mem);  // __init_user (static ctors)
extern void func_80006464(PPCContext* ctx, Memory* mem);  // main()

// Scene change request
extern void func_8000AC3C(PPCContext* ctx, Memory* mem);  // mDoGph_gInf_c::changeScene

// DVD queue completion handler
extern void func_80302288(PPCContext* ctx, Memory* mem);  // Process completed DVD requests

// Framework process creation
extern void func_80018430(PPCContext* ctx, Memory* mem);  // Execute creation request (call create func)
extern void func_80018BB8(PPCContext* ctx, Memory* mem);  // Process creation queue (0x803A72C0)

// JKRArchive resource lookup — we need to intercept this
extern void func_802B6FEC(PPCContext* ctx, Memory* mem);  // JKRFileLoader::getGlbResource

// From main01__Fv (0x80006338) — the REAL game loop:
extern void func_80006338(PPCContext* ctx, Memory* mem);  // main01 (init + game loop)
extern void func_8000C70C(PPCContext* ctx, Memory* mem);  // mDoCPd_Create (controller init)
extern void func_8000BC94(PPCContext* ctx, Memory* mem);  // mDoGph_Create (graphics init)
extern void func_80007A70(PPCContext* ctx, Memory* mem);  // mDoRst_Create (reset init)
extern void func_80023218(PPCContext* ctx, Memory* mem);  // fapGm_Create (framework create)
extern void func_80022DF8(PPCContext* ctx, Memory* mem);  // framework post-create init

// Scene manager per-frame dispatch sub-functions
extern void func_80007EE4(PPCContext* ctx, Memory* mem);  // Frame buffer swap
extern void func_803268D8(PPCContext* ctx, Memory* mem);  // Matrix identity setup


// Per-frame functions from main01's loop:
extern void func_800078C0(PPCContext* ctx, Memory* mem);  // mDoRst_Execute (reset check)
extern void func_80007224(PPCContext* ctx, Memory* mem);  // mDoAud_Execute (audio)
extern void func_800231E4(PPCContext* ctx, Memory* mem);  // fapGm_Execute (framework execute!)
extern void func_80006264(PPCContext* ctx, Memory* mem);  // main loop cleanup


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

// ---- Simple bump allocator for JKR heap replacement ----
// The game's JKRExpHeap system requires complex initialization that depends on
// mDoGph_Create running (which we skip). Instead, we replace the low-level
// allocator func_802B0434 with a simple bump allocator from the arena.
static uint32_t g_bump_alloc_ptr = 0x80400000;  // Start of arena
static const uint32_t BUMP_ALLOC_END = 0x81700000;  // End of arena

// ---- Fake JKR Archive Object ----
// We create a minimal fake JKRArchive-compatible object in emulated RAM so the
// game's scene creation code (func_80022CEC) can use it as if it were a real
// mounted archive. The object needs a valid vtable pointer and a "CASH" magic.
//
// Layout in emulated RAM (top of arena, well above game allocations):
//   0x817FFB00: Fake vtable (8 entries pointing to no-op function 0x8030150C)
//   0x817FFC00: Fake JKR object (88 bytes)
//     +0:  vtable ptr → 0x817FFB00
//     +44: 0x43415348 ("CASH" magic for mount list matching)
//     +52: refcount (0)
//     +72: mount point (non-null, points to root "/" string)
//   0x817FFE00: RARC buffer pointer (GC addr of decompressed archive)
//   0x817FFE04: RARC buffer size
//   0x817FFE08: "/" string for mount point

static const uint32_t FAKE_VTABLE_ADDR   = 0x817FFB00;
static const uint32_t FAKE_JKR_OBJ_ADDR  = 0x817FFC00;
static const uint32_t RARC_BUF_PTR_ADDR  = 0x817FFE00;
static const uint32_t RARC_BUF_SIZE_ADDR = 0x817FFE04;
static const uint32_t ROOT_STRING_ADDR   = 0x817FFE08;
static const uint32_t NOOP_FUNC_ADDR     = 0x8030150C;  // PPCHalt (replaced with no-op)

static void bump_alloc_replacement(PPCContext* ctx, Memory* mem) {
    // func_802B0434(r3=size, r4=align, r5=heap)
    // Ignores heap parameter, allocates from our bump arena.
    uint32_t size = ctx->r[3];
    int32_t align = (int32_t)ctx->r[4];
    if (align < 0) align = -align;  // Negative alignment = allocate from top
    if (align < 4) align = 4;

    // Align the allocation pointer
    uint32_t aligned = (g_bump_alloc_ptr + align - 1) & ~(align - 1);
    uint32_t end = aligned + size;

    if (end > BUMP_ALLOC_END || size == 0) {
        ctx->r[3] = 0;  // Allocation failed
        return;
    }

    g_bump_alloc_ptr = end;
    ctx->r[3] = aligned;
}

static void bump_free_replacement(PPCContext* ctx, Memory* mem) {
    // func_802B04FC(r3=ptr, r4=heap): free — no-op for bump allocator
    ctx->r[3] = 0;
}

// JKRHeap::getCurrentHeap — return a fake non-null heap pointer
static void get_current_heap_replacement(PPCContext* ctx, Memory* mem) {
    // Return a non-null value so callers don't think heap is uninitialized.
    // The actual heap object isn't used since we replace alloc/free.
    ctx->r[3] = 0x80400010;  // Fake heap object in arena
}


// DVD read from disc image — replacement for func_8030803C (DVDReadPrio).
// Uses gcrecomp's disc_read() API to read from the mounted ISO.
static void dvd_read_from_disc(PPCContext* ctx, Memory* mem) {
    uint32_t info_addr = ctx->r[3];
    uint32_t buf_addr  = ctx->r[6];
    uint32_t offset    = ctx->r[7];
    uint32_t length    = ctx->r[8];

    if (ww::is_disc_mounted() && buf_addr >= 0x80000000 && length > 0 && length < 0x01000000) {
        uint8_t* dst = mem->translate(buf_addr);
        if (dst) {
            size_t read = ww::disc_read(offset, dst, length);
            if (read == length) {
                mem->write16(info_addr + 712, 0);  // Status = done
                ctx->r[3] = 1;
                fprintf(stderr, "[DVD] Read %u bytes from disc offset 0x%08X → 0x%08X\n",
                        (unsigned)length, offset, buf_addr);
                return;
            }
        }
    }

    fprintf(stderr, "[DVD] Read failed: buf=0x%08X off=0x%08X len=%u\n",
            buf_addr, offset, (unsigned)length);
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

// Load FST (File System Table) from a GameCube ISO into emulated memory.
// The FST is needed for the game's own DVDConvertPathToEntrynum function to
// resolve file paths to disc offsets.

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
    // Force unbuffered stdout/stderr so output isn't lost when redirected
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    print_banner();

    // ---- Determine paths ----
    const char* dol_path = "main.dol";
    const char* iso_path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--iso") == 0 && i + 1 < argc) {
            iso_path = argv[++i];
        } else if (argv[i][0] != '-') {
            dol_path = argv[i];
        }
    }
    // Also check for ISO in current directory if not specified
    if (!iso_path) {
        // Try common ISO filenames
        static const char* iso_names[] = {
            "ww.iso", "game.iso",
            "Legend of Zelda, The - The Wind Waker.iso",
            nullptr
        };
        for (const char** name = iso_names; *name; name++) {
            FILE* test = fopen(*name, "rb");
            if (test) { fclose(test); iso_path = *name; break; }
        }
    }

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

    // ---- Mount disc image (optional) ----
    if (iso_path) {
        printf("[*] Mounting disc image: %s\n", iso_path);
        if (gcrecomp::mount_disc_image(iso_path, &g_mem)) {
            printf("[*] Disc mounted — DVD file access enabled.\n");
        } else {
            printf("[*] WARNING: Disc mount failed. Scene loading will not work.\n");
        }
    } else {
        printf("[*] No disc image (use --iso path.iso for scene loading).\n");
    }

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
    // Replace func_80022CEC — the original JKR volume search (func_802B6AB8)
    // dereferences uninitialized pointers from the empty mount list, causing
    // bad memory accesses. We stub it to return success and set the scene flag.
    g_func_table.register_func(0x80022CEC, [](PPCContext* ctx, Memory* mem) {
        if (mem->read32(ctx->r[13] - 30492) == 0) {
            mem->write32(ctx->r[13] - 30492, 1);
            fprintf(stderr, "[SCN] Scene process creation (stub)\n");
        }
        ctx->r[3] = 1;
    });
    // PPCHalt (0x8030150C): infinite spin loop → return immediately
    g_func_table.register_func(0x8030150C, idle_loop_replacement);
    // Scene manager execute (0x802558CC) — called per-frame via vtable.
    // Needs timing globals at r13(-26600) not yet initialized. No-op for now.
    g_func_table.register_func(0x802558CC, [](PPCContext* ctx, Memory* mem) {});
    // func_8000AF2C (mDoGph_gInf_c execute) — the scene manager's per-frame
    // dispatch. Phase 1 does scene state management (copies, vtable call, frame
    // swap). Phase 2 does camera/rendering setup that hangs without GX init.
    // We replace with Phase 1 only.
    g_func_table.register_func(0x8000AF2C, [](PPCContext* ctx, Memory* mem) {
        uint32_t r13 = ctx->r[13];
        uint32_t scene_mgr = mem->read32(r13 - 27984);
        if (scene_mgr == 0 || scene_mgr < 0x80000000) return;

        // Copy frame counter to scene manager
        uint32_t frame_val = mem->read32(r13 - 30792);
        mem->write32(scene_mgr + 4, frame_val);

        // Copy scene globals (r13-32736) to scene manager (+12..+15)
        uint32_t scene_info = mem->read32(r13 - 32736);
        mem->write8(scene_mgr + 12, (scene_info >> 24) & 0xFF);
        mem->write8(scene_mgr + 13, (scene_info >> 16) & 0xFF);
        mem->write8(scene_mgr + 14, (scene_info >> 8) & 0xFF);
        mem->write8(scene_mgr + 15, scene_info & 0xFF);

        // Call scene manager vtable[2] (execute — currently no-op'd)
        uint32_t vtable_ptr = mem->read32(scene_mgr);
        uint32_t exec_fn = mem->read32(vtable_ptr + 8);
        if (exec_fn != 0) {
            ctx->r[3] = scene_mgr;
            g_func_table.call(exec_fn, ctx, mem);
        }

        // Copy pending scene state to current (r13-32576 → r13-32736)
        for (int i = 0; i < 4; i++) {
            uint8_t b = mem->read8(r13 - 32576 + i);
            mem->write8(r13 - 32736 + i, b);
        }

        // Frame buffer swap (func_80007EE4)
        func_80007EE4(ctx, mem);
    });
    // Frame timing gate: always return "process frame"
    g_func_table.register_func(0x8003EBD4, frame_gate_replacement);
    // DVD read from disc image (for indirect calls)
    if (is_disc_mounted()) {
        g_func_table.register_func(0x8030803C, dvd_read_from_disc);
    }
    // Note: Hardware-dependent functions are patched directly in recompiled source
    // as no-op returns (direct bl calls bypass func_table overrides):
    //   func_800404CC — GX draw-done sync (recomp_0006.cpp)
    //   func_802C7788 — Display busy-wait / VIWaitForRetrace (recomp_0066.cpp)
    //   func_80006C4C — Exception handler setup (recomp_0000.cpp)
    //   func_802C79FC — Assert/panic handler (recomp_0066.cpp)
    //   func_802B0634 — Graphics buffer execute, NULL-safe (recomp_0063.cpp)

    // ---- Initialize Heap System (bump allocator replacement) ----
    // The game's JKRExpHeap requires mDoGph_Create which we skip (GX hardware).
    // We replace the low-level JKR allocator with a simple bump allocator and
    // set global heap pointers so the framework can allocate objects.
    printf("[*] Initializing heap (bump allocator)...\n");
    {
        // Replace JKR alloc/free with bump allocator
        g_func_table.register_func(0x802B0434, bump_alloc_replacement);
        g_func_table.register_func(0x802B04FC, bump_free_replacement);
        g_func_table.register_func(0x8001199C, get_current_heap_replacement);

        // Set root heap globals so JKR code paths don't hit NULL checks
        // r13(-27060) = root heap ptr, r13(-27056) = current heap ptr
        uint32_t fake_heap = 0x80400010;
        g_mem.write32(g_ctx.r[13] - 27060, fake_heap);  // sRootHeap
        g_mem.write32(g_ctx.r[13] - 27056, fake_heap);  // sCurrentHeap
        g_mem.write32(g_ctx.r[13] - 30640, fake_heap);  // getCurrentHeap result
        g_mem.write32(g_ctx.r[13] - 30648, fake_heap);  // func_800118C0 heap ptr
        g_mem.write32(g_ctx.r[13] - 30632, fake_heap);  // func_80011AB4 archive heap

        printf("[*] Bump allocator: 0x%08X - 0x%08X\n",
               g_bump_alloc_ptr, BUMP_ALLOC_END);

        // Set up fake JKR archive object for func_802B6FEC
        // Fake vtable: all 8 entries point to our no-op function (PPCHalt replacement)
        for (int i = 0; i < 8; i++) {
            g_mem.write32(FAKE_VTABLE_ADDR + i * 4, NOOP_FUNC_ADDR);
        }
        // Root "/" string
        g_mem.write8(ROOT_STRING_ADDR, '/');
        g_mem.write8(ROOT_STRING_ADDR + 1, 0);
        // Fake JKR object
        memset(g_mem.translate(FAKE_JKR_OBJ_ADDR), 0, 88);
        g_mem.write32(FAKE_JKR_OBJ_ADDR + 0, FAKE_VTABLE_ADDR);    // vtable
        g_mem.write32(FAKE_JKR_OBJ_ADDR + 44, 0x43415348);         // "CASH" magic
        g_mem.write32(FAKE_JKR_OBJ_ADDR + 52, 0);                  // refcount
        g_mem.write32(FAKE_JKR_OBJ_ADDR + 72, ROOT_STRING_ADDR);   // mount point "/"
        printf("[*] Fake JKR object at 0x%08X (vtable=0x%08X)\n",
               FAKE_JKR_OBJ_ADDR, FAKE_VTABLE_ADDR);
    }
    fflush(stdout);

    // ---- Initialize game framework (from main01__Fv = func_80006338) ----
    // main01 does: mDoCPd_Create, mDoGph_Create, mDoRst_Create, fapGm_Create,
    // then enters an infinite loop calling fapGm_Execute each frame.
    // We skip hardware-dependent init (controller, graphics mode) since we
    // handle those via host APIs, and run the game framework creation.
    printf("[*] Initializing game framework...\n");
    fflush(stdout);

    // Skip mDoCPd_Create (0x8000C70C) — hangs on PAD/SI hardware init
    // Skip mDoGph_Create (0x8000BC94) — graphics handled by D3D11, heap done above
    //   BUT: mDoGph_Create also creates the scene manager object. We create it manually.
    // Skip mDoRst_Create (0x80007A70) — reset controller, might need HW

    // ---- Create scene manager manually ----
    // mDoGph_Create normally creates this via func_80255354 → func_8025527C,
    // but those functions depend on GX/camera init which we skip. We create the
    // minimal object directly. The per-frame execute (func_802558CC) is no-op'd.
    {
        uint32_t mgr_addr = 0x817FFA00;
        memset(g_mem.translate(mgr_addr), 0, 64);
        g_mem.write32(mgr_addr + 0, 0x80395C20);    // vtable
        g_mem.write32(mgr_addr + 12, 0xFFFFFFFF);   // uninitialized sentinel
        g_mem.write32(mgr_addr + 28, 1);             // init flag (set by func_80007BBC)
        g_mem.write32(g_ctx.r[13] - 27984, mgr_addr);
        printf("[*]   Scene manager at 0x%08X\n", mgr_addr);
    }

    printf("[*]   fapGm_Create (game framework)...\n"); fflush(stdout);
    func_80023218(&g_ctx, &g_mem);
    printf("[*]   fapGm_Create done.\n"); fflush(stdout);

    printf("[*]   Framework post-init (func_80022DF8)...\n"); fflush(stdout);
    func_80022DF8(&g_ctx, &g_mem);
    printf("[*] Game framework initialized.\n");

    // Diagnostic: check creation queue and process tree state
    {
        uint32_t q_root = 0x803A72C0;
        // Dump first 64 bytes of creation queue structure
        // Creation queue at 0x803A72C0: check all key offsets
        printf("[*]   Creation Q: +36=0x%08X +40=0x%08X +44=0x%08X\n",
               g_mem.read32(q_root + 36), g_mem.read32(q_root + 40),
               g_mem.read32(q_root + 44));
        printf("[*]   Creation Q: +0=0x%08X +4=0x%08X +8=0x%08X +12=0x%08X\n",
               g_mem.read32(q_root + 0), g_mem.read32(q_root + 4),
               g_mem.read32(q_root + 8), g_mem.read32(q_root + 12));
        // Process tree
        printf("[*]   Tree: +0=0x%08X +4=0x%08X +8=0x%08X\n",
               g_mem.read32(0x803726A0 + 0), g_mem.read32(0x803726A0 + 4),
               g_mem.read32(0x803726A0 + 8));
        // Root scene process object dump
        uint32_t root_scn = g_mem.read32(g_ctx.r[13] - 30488);
        printf("[*]   Root scene @r13(-30488) = 0x%08X\n", root_scn);
        if (root_scn >= 0x80000000 && root_scn < 0x82000000) {
            printf("[*]     Scene obj: +0=0x%08X +4=0x%08X +8=0x%08X +12=0x%08X\n",
                   g_mem.read32(root_scn), g_mem.read32(root_scn + 4),
                   g_mem.read32(root_scn + 8), g_mem.read32(root_scn + 12));
            printf("[*]     Scene obj: +16=0x%08X +20=0x%08X +24=0x%08X +28=0x%08X\n",
                   g_mem.read32(root_scn + 16), g_mem.read32(root_scn + 20),
                   g_mem.read32(root_scn + 24), g_mem.read32(root_scn + 28));
        }
        printf("[*]   Framework dispatch @0x803950D8+16 = 0x%08X\n",
               g_mem.read32(0x803950D8 + 16));
        // Handler vtable at 0x80371D58
        printf("[*]   Handler vtable @0x80371D58:\n");
        for (int i = 0; i < 8; i++) {
            printf("[*]     [%d] = 0x%08X\n", i, g_mem.read32(0x80371D58 + i*4));
        }
        // Check the create thread proc queue address
        printf("[*]   Ready queue @0x803BCEC8: +0=0x%08X +4=0x%08X +8=0x%08X\n",
               g_mem.read32(0x803BCEC8), g_mem.read32(0x803BCEC8 + 4),
               g_mem.read32(0x803BCEC8 + 8));
        // Check what resource path the scene create function looks for
        // func_80022CEC calls func_802B6FEC(0x8033BB44, heap, 0)
        {
            uint32_t str_addr = 0x8033BB44;
            uint8_t* p = g_mem.translate(str_addr);
            char path[64] = {};
            if (p) for (int i = 0; i < 63 && p[i]; i++) path[i] = p[i];
            printf("[*]   Scene create resource path @0x%08X: \"%s\"\n", str_addr, path);
            // Also check the JKR mount list at 0x803ED77C
            uint32_t mount_list = g_mem.read32(0x803ED77C);
            printf("[*]   JKR mount list @0x803ED77C = 0x%08X\n", mount_list);
        }
    }
    fflush(stdout);

    // Process any DVD requests enqueued during framework init.
    // On real hardware, these complete via DI interrupt during init.
    {
        uint32_t dvd_q = g_mem.read32(g_ctx.r[13] - 26400);
        if (dvd_q != 0) {
            uint32_t cb = g_mem.read32(dvd_q + 0);
            printf("[*] Processing framework DVD request (queue=0x%08X, cb=0x%08X)...\n",
                   dvd_q, cb);
            g_ctx.r[3] = dvd_q;
            func_80302288(&g_ctx, &g_mem);
            printf("[*] Framework DVD request processed.\n");
        }
    }

    // ---- Trigger initial scene load ----
    // The scene loading state machine (in func_8000AF2C) starts at state 0 and
    // never progresses because func_8000AC3C (scene change request) is never called.
    // In the original game, the boot sequence sets scene globals and triggers loading.
    // We manually set the starting scene to "sea_T" (title screen).
    printf("[*] Setting initial scene (sea_T)...\n");
    {
        uint32_t r13_val = g_ctx.r[13];
        // Stage name string — write "sea_T" to the stage name global
        // dComIfG_gameInfo.play.mStartStage stores the stage name
        // at r13(-32720) area (0x803F80B0)
        uint32_t stage_area = r13_val - 32720;  // 0x803F80B0
        // Write stage name "sea_T\0" (8 bytes)
        g_mem.write8(stage_area + 0, 's');
        g_mem.write8(stage_area + 1, 'e');
        g_mem.write8(stage_area + 2, 'a');
        g_mem.write8(stage_area + 3, '_');
        g_mem.write8(stage_area + 4, 'T');
        g_mem.write8(stage_area + 5, 0);

        // Scene type flag — set to 14 (0x0E) to trigger loading path
        g_mem.write8(r13_val - 32719, 14);   // scene type = 14

        // Room 44 = sea stage first room, Layer 0xFF = all layers
        g_mem.write8(r13_val - 32717, 44);    // room index
        g_mem.write8(r13_val - 32716, 0xFF);  // layer = all
        g_mem.write16(r13_val - 32714, 0);    // spawn point = 0
        g_mem.write16(r13_val - 32712, 0);    // parameter = 0

        printf("[*] Scene globals set: type=%d room=%d layer=%d spawn=%d\n",
               g_mem.read8(r13_val - 32719),
               g_mem.read8(r13_val - 32717),
               g_mem.read8(r13_val - 32716),
               (int16_t)g_mem.read16(r13_val - 32714));

        // Debug: check the scene type dispatch table
        uint32_t jtable = 0x803A1C08;
        printf("[*] Scene type 14 dispatch: jump_table[14] = 0x%08X\n",
               g_mem.read32(jtable + 14 * 4));
        printf("[*] Scene type  6 dispatch: jump_table[6] = 0x%08X\n",
               g_mem.read32(jtable + 6 * 4));
        printf("[*] Scene type  0 dispatch: jump_table[0] = 0x%08X\n",
               g_mem.read32(jtable + 0 * 4));

        // Trigger the scene change request
        printf("[*] Requesting scene change...\n");
        fflush(stdout);
        // Dump queue BEFORE scene change
        uint32_t q_before = g_mem.read32(r13_val - 26400);
        uint32_t cb_before = q_before ? g_mem.read32(q_before + 0) : 0;
        printf("[*]   Queue before: 0x%08X (cb=0x%08X)\n", q_before, cb_before);

        func_8000AC3C(&g_ctx, &g_mem);
        int16_t scene_state = (int16_t)g_mem.read16(r13_val - 30754);
        printf("[*] Scene change result: state=%d %s\n", scene_state,
               scene_state == -1 ? "(failed — no disc data)" :
               scene_state == 0 ? "(reset)" : "(ok)");

        // Dump queue AFTER scene change
        uint32_t q_after = g_mem.read32(r13_val - 26400);
        uint32_t cb_after = q_after ? g_mem.read32(q_after + 0) : 0;
        printf("[*]   Queue after: 0x%08X (cb=0x%08X)\n", q_after, cb_after);
        printf("[*]   r13(-30728) = 0x%08X (load handle)\n",
               g_mem.read32(r13_val - 30728));
        printf("[*]   r13(-30712) = 0x%08X, r13(-30708) = 0x%08X (disc offsets)\n",
               g_mem.read32(r13_val - 30712), g_mem.read32(r13_val - 30708));

        // Complete the DVD request: read Stage.arc from ISO, decompress Yaz0,
        // parse RARC, and place decompressed data where the game expects it.
        // The game's DVD thread normally handles this pipeline:
        //   1. Read compressed data from disc
        //   2. Yaz0 decompress
        //   3. Place decompressed RARC in the target buffer
        //   4. Fire completion callback
        if (scene_state == 1 && is_disc_mounted()) {
            // sea_T/Stage.arc is at disc offset 0x5120DF1C, size 59889 bytes
            const uint32_t STAGE_ARC_OFFSET = 0x5120DF1C;
            const uint32_t STAGE_ARC_SIZE   = 59889;

            uint32_t buf1_addr = g_mem.read32(r13_val - 30740);  // first buffer

            printf("[*] Loading Stage.arc (%u bytes) from ISO...\n", STAGE_ARC_SIZE);

            if (buf1_addr >= 0x80000000) {
                // Read compressed data into a temporary host buffer
                std::vector<uint8_t> compressed(STAGE_ARC_SIZE);
                size_t read = disc_read(STAGE_ARC_OFFSET, compressed.data(), STAGE_ARC_SIZE);
                printf("[*]   Read %zu bytes from disc\n", read);

                if (read == STAGE_ARC_SIZE) {
                    // Check if it's Yaz0 compressed
                    if (gcrecomp::yaz0_is_compressed(compressed.data(), compressed.size())) {
                        uint32_t decomp_size = gcrecomp::yaz0_decompressed_size(
                            compressed.data(), compressed.size());
                        printf("[*]   Yaz0 compressed: %u → %u bytes\n",
                               STAGE_ARC_SIZE, decomp_size);

                        // Decompress into emulated RAM at the allocated buffer
                        uint8_t* dst = g_mem.translate(buf1_addr);
                        size_t written = gcrecomp::yaz0_decompress(
                            compressed.data(), compressed.size(),
                            dst, decomp_size);

                        if (written == decomp_size) {
                            printf("[*]   Yaz0 decompressed %zu bytes → 0x%08X\n",
                                   written, buf1_addr);
                            // Store RARC buffer location for JKR resource lookups
                            g_mem.write32(RARC_BUF_PTR_ADDR, buf1_addr);
                            g_mem.write32(RARC_BUF_SIZE_ADDR, decomp_size);

                            // Verify RARC header
                            if (gcrecomp::rarc_is_archive(dst, written)) {
                                gcrecomp::RARCArchive archive;
                                if (gcrecomp::rarc_parse(dst, written, archive)) {
                                    printf("[*]   RARC archive contents:\n");
                                    for (const auto& f : archive.files) {
                                        printf("[*]     %s (%u bytes)\n",
                                               f.path.c_str(), f.data_size);
                                    }
                                } else {
                                    printf("[*]   WARNING: RARC parse failed\n");
                                }
                            } else {
                                printf("[*]   Header: %02X%02X%02X%02X (not RARC?)\n",
                                       dst[0], dst[1], dst[2], dst[3]);
                            }
                        } else {
                            printf("[*]   WARNING: Yaz0 decompression incomplete\n");
                        }
                    } else {
                        // Not compressed — copy directly to emulated RAM
                        uint8_t* dst = g_mem.translate(buf1_addr);
                        memcpy(dst, compressed.data(), compressed.size());
                        printf("[*]   Copied %zu bytes (uncompressed) → 0x%08X\n",
                               compressed.size(), buf1_addr);
                    }
                }
            }

            // Set state to 2 and invoke completion callback
            printf("[*] Invoking DVD completion callback...\n");
            g_mem.write16(r13_val - 30754, 2);  // state = 2
            uint32_t dvd_q = g_mem.read32(r13_val - 26400);
            if (dvd_q != 0) {
                uint32_t cb = g_mem.read32(dvd_q + 0);
                if (cb != 0) {
                    g_ctx.r[3] = dvd_q;
                    g_ctx.r[4] = 0;
                    g_func_table.call(cb, &g_ctx, &g_mem);
                    int16_t new_state = (int16_t)g_mem.read16(r13_val - 30754);
                    printf("[*]   After callback: state=%d\n", new_state);
                }
            }
        } else if (scene_state == 1) {
            printf("[*] Scene change pending but no disc mounted.\n");
        }
    }
    fflush(stdout);

    // ---- Load Room44.arc from disc ----
    // Bypass the framework process system and load room data directly.
    // The scene data (Stage.arc) is already decompressed at buf1_addr.
    // Now load the room archive using the same Yaz0+RARC pipeline.
    if (is_disc_mounted()) {
        // sea_T/Room44.arc: offset=0x51245A50, size=714816
        const uint32_t ROOM44_ARC_OFFSET = 0x51245A50;
        const uint32_t ROOM44_ARC_SIZE   = 714816;

        printf("[*] Loading Room44.arc (%u bytes, %uKB) from ISO...\n",
               ROOM44_ARC_SIZE, ROOM44_ARC_SIZE / 1024);

        std::vector<uint8_t> room_compressed(ROOM44_ARC_SIZE);
        size_t room_read = disc_read(ROOM44_ARC_OFFSET, room_compressed.data(), ROOM44_ARC_SIZE);
        printf("[*]   Read %zu bytes from disc\n", room_read);

        if (room_read == ROOM44_ARC_SIZE) {
            if (gcrecomp::yaz0_is_compressed(room_compressed.data(), room_compressed.size())) {
                uint32_t room_decomp_size = gcrecomp::yaz0_decompressed_size(
                    room_compressed.data(), room_compressed.size());
                printf("[*]   Yaz0 compressed: %u → %u bytes (%uKB)\n",
                       ROOM44_ARC_SIZE, room_decomp_size, room_decomp_size / 1024);

                // Allocate space in emulated RAM for decompressed room data
                uint32_t room_align = (g_bump_alloc_ptr + 31) & ~31;
                if (room_align + room_decomp_size < BUMP_ALLOC_END) {
                    uint32_t room_buf_addr = room_align;
                    g_bump_alloc_ptr = room_align + room_decomp_size;

                    uint8_t* room_dst = g_mem.translate(room_buf_addr);
                    size_t room_written = gcrecomp::yaz0_decompress(
                        room_compressed.data(), room_compressed.size(),
                        room_dst, room_decomp_size);

                    if (room_written == room_decomp_size) {
                        printf("[*]   Yaz0 decompressed %zu bytes → 0x%08X\n",
                               room_written, room_buf_addr);

                        if (gcrecomp::rarc_is_archive(room_dst, room_written)) {
                            gcrecomp::RARCArchive room_archive;
                            if (gcrecomp::rarc_parse(room_dst, room_written, room_archive)) {
                                printf("[*]   Room44.arc RARC contents (%zu files):\n",
                                       room_archive.files.size());
                                for (const auto& f : room_archive.files) {
                                    printf("[*]     %s (%u bytes)\n",
                                           f.path.c_str(), f.data_size);
                                }
                            } else {
                                printf("[*]   WARNING: Room44 RARC parse failed\n");
                            }
                        } else {
                            printf("[*]   Header: %02X%02X%02X%02X (not RARC?)\n",
                                   room_dst[0], room_dst[1], room_dst[2], room_dst[3]);
                        }
                    } else {
                        printf("[*]   WARNING: Room44 Yaz0 decompression incomplete\n");
                    }
                } else {
                    printf("[*]   WARNING: Not enough arena space for Room44 (%u bytes)\n",
                           room_decomp_size);
                }
            } else {
                // Not Yaz0 compressed — raw RARC archive
                uint32_t room_size = (uint32_t)room_compressed.size();
                uint32_t room_align = (g_bump_alloc_ptr + 31) & ~31;
                if (room_align + room_size < BUMP_ALLOC_END) {
                    uint32_t room_buf_addr = room_align;
                    g_bump_alloc_ptr = room_align + room_size;

                    uint8_t* room_dst = g_mem.translate(room_buf_addr);
                    memcpy(room_dst, room_compressed.data(), room_size);
                    printf("[*]   Copied %u bytes (uncompressed) → 0x%08X\n",
                           room_size, room_buf_addr);

                    if (gcrecomp::rarc_is_archive(room_dst, room_size)) {
                        gcrecomp::RARCArchive room_archive;
                        if (gcrecomp::rarc_parse(room_dst, room_size, room_archive)) {
                            printf("[*]   Room44.arc RARC contents (%zu files):\n",
                                   room_archive.files.size());
                            for (const auto& f : room_archive.files) {
                                printf("[*]     %s (%u bytes)\n",
                                       f.path.c_str(), f.data_size);
                            }
                        } else {
                            printf("[*]   WARNING: Room44 RARC parse failed\n");
                        }
                    } else {
                        printf("[*]   Header: %02X%02X%02X%02X (not RARC?)\n",
                               room_dst[0], room_dst[1], room_dst[2], room_dst[3]);
                    }
                }
            }
        }

        // Also parse stage.dzs from the already-loaded Stage.arc
        uint32_t stage_buf = g_mem.read32(RARC_BUF_PTR_ADDR);
        uint32_t stage_size = g_mem.read32(RARC_BUF_SIZE_ADDR);
        if (stage_buf >= 0x80000000 && stage_size > 0) {
            uint8_t* stage_data = g_mem.translate(stage_buf);
            gcrecomp::RARCArchive stage_archive;
            if (gcrecomp::rarc_parse(stage_data, stage_size, stage_archive)) {
                const gcrecomp::RARCFile* dzs = stage_archive.find_path("dzs/stage.dzs");
                if (dzs) {
                    const uint8_t* dzs_data = stage_archive.file_data(*dzs, stage_data, stage_size);
                    if (dzs_data) {
                        printf("[*] stage.dzs: %u bytes at %p\n", dzs->data_size, dzs_data);
                        // DZS header: first 4 bytes = chunk count
                        if (dzs->data_size >= 8) {
                            uint32_t chunk_count = (dzs_data[0] << 24) | (dzs_data[1] << 16) |
                                                   (dzs_data[2] << 8) | dzs_data[3];
                            printf("[*]   DZS chunk count: %u\n", chunk_count);
                            // Each chunk header: 4-byte tag + 4-byte count + 4-byte offset
                            for (uint32_t i = 0; i < chunk_count && (4 + i * 12 + 12) <= dzs->data_size; i++) {
                                const uint8_t* ch = dzs_data + 4 + i * 12;
                                char tag[5] = {(char)ch[0], (char)ch[1], (char)ch[2], (char)ch[3], 0};
                                uint32_t cnt = (ch[4] << 24) | (ch[5] << 16) | (ch[6] << 8) | ch[7];
                                uint32_t off = (ch[8] << 24) | (ch[9] << 16) | (ch[10] << 8) | ch[11];
                                printf("[*]     Chunk '%s': %u entries at offset 0x%X\n", tag, cnt, off);
                            }
                        }
                    }
                }
            }
        }


        // ---- Parse BDL models from Room44.arc ----
        // Re-parse Room44.arc to access BDL files (room_compressed still in scope)
        if (!room_compressed.empty()) {
            const uint8_t* room_raw = room_compressed.data();
            uint32_t room_raw_size = (uint32_t)room_compressed.size();
            if (gcrecomp::rarc_is_archive(room_raw, room_raw_size)) {
                gcrecomp::RARCArchive room_arc;
                if (gcrecomp::rarc_parse(room_raw, room_raw_size, room_arc)) {
                    // Parse each BDL file — keep the main room model for rendering
                    const char* bdl_names[] = {"bdl/model.bdl", "bdl/model1.bdl", "bdl/model3.bdl", nullptr};
                    for (const char** name = bdl_names; *name; name++) {
                        const gcrecomp::RARCFile* f = room_arc.find_path(*name);
                        if (f) {
                            const uint8_t* bdl_data = room_arc.file_data(*f, room_raw, room_raw_size);
                            if (bdl_data) {
                                printf("[*] Parsing %s (%u bytes)...\n", *name, f->data_size);
                                j3d::J3DModel model;
                                if (j3d::j3d_parse(bdl_data, f->data_size, model)) {
                                    j3d::j3d_print_summary(model);
                                    // Keep main room model for rendering
                                    if (strcmp(*name, "bdl/model.bdl") == 0) {
                                        g_room_model = std::move(model);
                                        g_room_bdl_data = bdl_data;
                                        g_room_bdl_size = f->data_size;
                                        g_room_model_loaded = true;
                                        printf("[*]   Stored for rendering.\n");
                                    }
                                } else {
                                    printf("[*]   J3D parse failed\n");
                                }
                            }
                        }
                    }

                    // Also parse skybox models from Stage.arc
                    uint32_t sbuf = g_mem.read32(RARC_BUF_PTR_ADDR);
                    uint32_t ssz = g_mem.read32(RARC_BUF_SIZE_ADDR);
                    if (sbuf >= 0x80000000 && ssz > 0) {
                        uint8_t* sdata = g_mem.translate(sbuf);
                        gcrecomp::RARCArchive sarc;
                        if (gcrecomp::rarc_parse(sdata, ssz, sarc)) {
                            const char* sky_names[] = {"bdl/vr_sky.bdl", "bdl/vr_uso_umi.bdl", nullptr};
                            for (const char** name = sky_names; *name; name++) {
                                const gcrecomp::RARCFile* f = sarc.find_path(*name);
                                if (f) {
                                    const uint8_t* bdl_data = sarc.file_data(*f, sdata, ssz);
                                    if (bdl_data) {
                                        printf("[*] Parsing %s (%u bytes)...\n", *name, f->data_size);
                                        j3d::J3DModel model;
                                        if (j3d::j3d_parse(bdl_data, f->data_size, model)) {
                                            j3d::j3d_print_summary(model);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    fflush(stdout);

    // ---- Diagnostics: check framework state ----
    {
        uint32_t r13_val = g_ctx.r[13];  // 0x803FE0E0
        printf("[*] Diagnostics:\n");
        printf("[*]   r13 = 0x%08X\n", r13_val);

        // SDA-relative offsets are signed 16-bit: r13 + (int16_t)offset
        // r13(-30488) = 0x803FE0E0 - 30488 = 0x803F69C8
        uint32_t scene_addr = r13_val - 30488;
        uint32_t scene_ptr = g_mem.read32(scene_addr);
        printf("[*]   Root scene ptr @0x%08X (r13-30488) = 0x%08X\n", scene_addr, scene_ptr);

        // r13(-30372) = 0x803FE0E0 - 30372 = 0x803F6A3C
        uint32_t lc_addr = r13_val - 30372;
        uint32_t layer_count = g_mem.read32(lc_addr);
        printf("[*]   Layer count @0x%08X (r13-30372) = %u\n", lc_addr, layer_count);

        // r13(-30376)
        uint32_t val30376 = g_mem.read32(r13_val - 30376);
        printf("[*]   r13-30376 @0x%08X = 0x%08X\n", r13_val - 30376, val30376);

        // r13(-30368)
        uint32_t val30368 = g_mem.read32(r13_val - 30368);
        printf("[*]   r13-30368 @0x%08X = 0x%08X\n", r13_val - 30368, val30368);

        // r13(-30348) — VRetrace state (frame gate reads this)
        uint32_t vretrace = g_mem.read32(r13_val - 30348);
        printf("[*]   VRetrace state @0x%08X (r13-30348) = 0x%08X\n", r13_val - 30348, vretrace);

        // Framework state at 0x803B9A00
        uint8_t fw_state = g_mem.read8(0x803B9A00);
        printf("[*]   Framework state (0x803B9A00) = 0x%02X\n", fw_state);

        // fapGm global at 0x803A5778
        uint32_t fapgm_global = g_mem.read32(0x803A5778);
        printf("[*]   fapGm global (0x803A5778) = 0x%08X\n", fapgm_global);

        // Check the scene/layer table at 0x803A22F8 (fapGm_Create passes this)
        uint32_t table_ptr = g_mem.read32(0x803A22F8);
        printf("[*]   fapGm table @0x803A22F8 = 0x%08X\n", table_ptr);

        fflush(stdout);
    }

    // ---- Launch Game Thread ----
    printf("[*] Launching game thread (main01 loop)...\n");
    printf("[*] (Press ESC to quit)\n\n");

    g_game_running = true;
    std::thread game_thread([&]() {
        fprintf(stderr, "[*] Entering main game loop (fapGm_Execute per frame)...\n");

        int frame = 0;
        while (g_game_running) {
            func_800078C0(&g_ctx, &g_mem);  // mDoRst_Execute
            func_80007224(&g_ctx, &g_mem);  // mDoAud_Execute

            // Pump DVD request queue — process pending async requests.
            // On real hardware, the DVD thread + DI interrupts handle this:
            //   1. Read compressed data from disc
            //   2. Yaz0 decompress in-place
            //   3. Fire completion callback
            // We emulate this by checking if the loaded data is Yaz0-compressed
            // and decompressing it before invoking the callback.
            {
                uint32_t dvd_q = g_mem.read32(g_ctx.r[13] - 26400);
                int16_t cur_state = (int16_t)g_mem.read16(g_ctx.r[13] - 30754);
                if (dvd_q != 0 && cur_state == 1 && is_disc_mounted()) {
                    uint32_t cb = g_mem.read32(dvd_q + 0);
                    if (cb != 0) {
                        // Check if the loaded buffer contains Yaz0 data and decompress
                        uint32_t buf_addr = g_mem.read32(g_ctx.r[13] - 30740);
                        if (buf_addr >= 0x80000000) {
                            uint8_t* buf = g_mem.translate(buf_addr);
                            if (buf && gcrecomp::yaz0_is_compressed(buf, 16)) {
                                uint32_t decomp_size = gcrecomp::yaz0_decompressed_size(buf, 16);
                                // Decompress to a temp buffer then copy back
                                // (can't decompress in-place since source overlaps dest)
                                auto decompressed = gcrecomp::yaz0_decompress(buf, decomp_size * 2);
                                if (!decompressed.empty()) {
                                    memcpy(buf, decompressed.data(), decompressed.size());
                                    if (frame <= 5) {
                                        fprintf(stderr, "[DVD] Yaz0 decompressed %zu bytes in buffer 0x%08X\n",
                                                decompressed.size(), buf_addr);
                                    }
                                }
                            }
                        }

                        // Advance state and invoke callback
                        g_mem.write16(g_ctx.r[13] - 30754, 2);
                        // Clear queue head
                        g_mem.write32(g_ctx.r[13] - 26400, 0);
                        // Call callback
                        g_ctx.r[3] = dvd_q;
                        g_ctx.r[4] = 0;
                        g_func_table.call(cb, &g_ctx, &g_mem);
                        if (frame <= 5) {
                            fprintf(stderr, "[DVD] Processed queue entry (cb=0x%08X)\n", cb);
                        }
                    }
                }
            }

            // Pump framework creation queue — process pending creation requests.
            // The original game has a background thread that processes these.
            // In our single-threaded model, we process them per-frame.
            {
                const uint32_t CREATE_Q = 0x803A72C0;
                uint32_t pending_count = g_mem.read32(CREATE_Q + 44);
                if (pending_count > 0) {
                    uint32_t item_addr = g_mem.read32(CREATE_Q + 36);
                    uint32_t sentinel = CREATE_Q + 36;
                    while (item_addr != 0 && item_addr != sentinel) {
                        uint8_t created = g_mem.read8(item_addr + 12);
                        if (created == 0) {
                            g_ctx.r[3] = item_addr;
                            func_80018430(&g_ctx, &g_mem);
                            if (frame <= 5) {
                                fprintf(stderr, "[FW] Creation: item+12=%u item+28=0x%08X sp=0x%08X\n",
                                        g_mem.read8(item_addr + 12),
                                        g_mem.read32(item_addr + 28),
                                        g_ctx.r[1]);
                            }
                        }
                        item_addr = g_mem.read32(item_addr + 4);
                    }
                }
            }

            func_800231E4(&g_ctx, &g_mem);  // fapGm_Execute
            func_80006264(&g_ctx, &g_mem);  // main loop cleanup

            frame++;
            if (frame <= 10 || frame % 60 == 0) {
                int16_t ss = (int16_t)g_mem.read16(g_ctx.r[13] - 30754);
                uint32_t dq = g_mem.read32(g_ctx.r[13] - 26400);
                uint8_t scene_flag = g_mem.read8(0x803CA701);
                // Process tree root at 0x803726A0 (fapGm process list)
                uint32_t tree_ptr = g_mem.read32(0x803726A0);
                uint32_t tree_count = g_mem.read32(0x803726A0 + 8);
                // Scene manager at r13(-27984)
                uint32_t scene_mgr = g_mem.read32(g_ctx.r[13] - 27984);
                fprintf(stderr, "[*] Frame %d (state=%d dvdq=0x%X flag=0x%X tree=0x%X/%u scnmgr=0x%X)\n",
                        frame, ss, dq, scene_flag,
                        tree_ptr, tree_count, scene_mgr);
            }

            Sleep(16);  // ~60 FPS
        }

        fprintf(stderr, "\n[*] Game thread exiting after %d frames.\n", frame);
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

        // ---- Render room geometry ----
        if (g_room_model_loaded && !g_room_model.vertex_arrays.empty()) {
            using namespace gcrecomp::gx;

            // Find vertex arrays
            const j3d::VertexArray* pos_array = nullptr;
            const j3d::VertexArray* tc_array = nullptr;
            for (const auto& va : g_room_model.vertex_arrays) {
                if (va.attr == j3d::GX_VA_POS) pos_array = &va;
                if (va.attr == j3d::GX_VA_TEX0) tc_array = &va;
            }

            if (pos_array && pos_array->count > 0 && pos_array->comp_type == 4 /*f32*/) {
                // Rotate camera slowly
                g_camera_angle += 0.005f;
                float ca = cosf(g_camera_angle), sa = sinf(g_camera_angle);
                float dist = 8000.0f;  // Camera distance
                float eye_x = sa * dist, eye_z = ca * dist, eye_y = 3000.0f;

                // Simple look-at model-view matrix (row-major 3x4)
                // Forward = normalize(-eye)
                float fx = -eye_x, fy = -eye_y, fz = -eye_z;
                float fl = sqrtf(fx*fx + fy*fy + fz*fz);
                fx /= fl; fy /= fl; fz /= fl;
                // Right = normalize(cross(up, forward))
                float rx = fz, ry = 0.0f, rz = -fx;  // up=(0,1,0) simplified
                float rl = sqrtf(rx*rx + rz*rz);
                if (rl > 0.001f) { rx /= rl; rz /= rl; }
                // Up = cross(forward, right)
                float ux = fy*rz - fz*ry, uy = fz*rx - fx*rz, uz = fx*ry - fy*rx;

                float mv[3][4] = {
                    { rx, ry, rz, -(rx*eye_x + ry*eye_y + rz*eye_z) },
                    { ux, uy, uz, -(ux*eye_x + uy*eye_y + uz*eye_z) },
                    { fx, fy, fz, -(fx*eye_x + fy*eye_y + fz*eye_z) },
                };
                GXLoadPosMtxImm(mv, 0);
                GXSetCurrentMtx(0);

                // Perspective projection
                float aspect = 1280.0f / 720.0f;
                float fovy = 60.0f * 3.14159f / 180.0f;
                float near_z = 100.0f, far_z = 50000.0f;
                float t = tanf(fovy * 0.5f);
                float proj[4][4] = {};
                proj[0][0] = 1.0f / (aspect * t);
                proj[1][1] = 1.0f / t;
                proj[2][2] = -(far_z + near_z) / (far_z - near_z);
                proj[2][3] = -2.0f * far_z * near_z / (far_z - near_z);
                proj[3][2] = -1.0f;
                GXSetProjection(proj, 0); // 0 = perspective

                GXSetViewport(0, 0, 1280, 720, 0.0f, 1.0f);

                // Load textures once
                if (!g_textures_loaded && !g_room_model.textures.empty()) {
                    g_textures_loaded = true;
                    uint32_t ntex = std::min((uint32_t)g_room_model.textures.size(), 8u);
                    for (uint32_t t = 0; t < ntex; t++) {
                        const auto& th = g_room_model.textures[t];
                        if (th.image_data && th.width > 0 && th.height > 0) {
                            GXInitTexObj(&g_tex_objs[t], th.image_data,
                                        th.width, th.height,
                                        (GXTexFmt)th.format,
                                        th.wrap_s, th.wrap_t, th.mipmap_count > 1);
                            printf("[GFX] Loaded texture %u: %s %ux%u fmt=%u\n",
                                   t, th.name.c_str(), th.width, th.height, th.format);
                        }
                    }
                }

                // TEV: modulate texture with vertex color
                // Stage 0: output = TEXC * RASC (texture color * vertex color)
                if (g_textures_loaded) {
                    GXLoadTexObj(&g_tex_objs[0], 0); // bind texture 0 to map 0
                    GXSetNumTevStages(1);
                    GXSetTevOrder(GX_TEVSTAGE0, 0, 0, 0); // texcoord0, texmap0, color0
                    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
                    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
                    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
                    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
                } else {
                    // Fallback: vertex color only
                    GXSetNumTevStages(1);
                    GXSetTevOrder(GX_TEVSTAGE0, 0xFF, 0xFF, 0);
                    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
                    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
                    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
                    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
                }

                GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, 0);
                GXSetZMode(true, GX_LEQUAL, true);
                GXSetCullMode(GX_CULL_NONE);

                // Vertex format: direct position + direct color + direct texcoord
                GXClearVtxDesc();
                GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
                GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
                if (tc_array && g_textures_loaded) {
                    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
                }

                // Render room geometry using indexed display lists from SHP1.
                // Each shape batch contains triangle strips/fans with 16-bit
                // indices into the VTX1 position/color/texcoord arrays.
                const uint8_t* pos_data = pos_array->data;
                uint32_t pos_count = pos_array->count;

                for (const auto& shape : g_room_model.shapes) {
                    for (const auto& pkt : shape.packets) {
                        if (!pkt.display_list || pkt.display_list_size < 4) continue;
                        const uint8_t* dl = pkt.display_list;
                        uint32_t dl_end = pkt.display_list_size;
                        uint32_t dp = 0;

                        // Calculate bytes per vertex from batch attributes
                        uint32_t bpv = 0;
                        for (const auto& a : shape.attribs) {
                            if (a.data_type == 1) bpv += 1;      // direct byte
                            else if (a.data_type == 2) bpv += 1;  // index8
                            else if (a.data_type == 3) bpv += 2;  // index16
                        }
                        if (bpv == 0) continue;

                        while (dp < dl_end) {
                            uint8_t cmd = dl[dp];
                            if (cmd == 0) { dp++; continue; } // NOP padding
                            if (cmd < 0x80) { dp++; continue; } // skip non-draw

                            uint8_t prim_type = cmd & 0xF8;
                            if (dp + 3 > dl_end) break;
                            uint16_t vert_count = j3d::read16(dl + dp + 1);
                            dp += 3;

                            if (vert_count == 0 || dp + vert_count * bpv > dl_end) break;

                            // Collect triangle strip/fan vertices into a flat list
                            struct Vtx { float x, y, z; uint8_t r, g, b; float s, t; };
                            std::vector<Vtx> verts(vert_count);

                            uint32_t tc_count = tc_array ? tc_array->count : 0;
                            const uint8_t* tc_data = tc_array ? tc_array->data : nullptr;
                            uint32_t tc_stride = tc_array ? tc_array->stride : 0;
                            uint32_t tc_type = tc_array ? tc_array->comp_type : 0;
                            uint32_t tc_frac = tc_array ? tc_array->frac_bits : 0;

                            for (uint16_t v = 0; v < vert_count; v++) {
                                uint16_t pos_idx = 0, tc_idx = 0;
                                for (const auto& a : shape.attribs) {
                                    uint16_t idx = 0;
                                    if (a.data_type == 2) { idx = dl[dp]; dp += 1; }
                                    else if (a.data_type == 3) { idx = j3d::read16(dl + dp); dp += 2; }
                                    else if (a.data_type == 1) { dp += 1; continue; }
                                    else continue;
                                    if (a.attr == 9) pos_idx = idx; // GX_VA_POS
                                    if (a.attr == 13) tc_idx = idx; // GX_VA_TEX0
                                }
                                if (pos_idx < pos_count) {
                                    verts[v].x = j3d::readf32(pos_data + pos_idx * 12 + 0);
                                    verts[v].y = j3d::readf32(pos_data + pos_idx * 12 + 4);
                                    verts[v].z = j3d::readf32(pos_data + pos_idx * 12 + 8);
                                } else {
                                    verts[v] = {0,0,0, 128,128,128, 0,0};
                                }
                                // Texcoord lookup (s16 with frac_bits)
                                if (tc_data && tc_idx < tc_count && tc_stride >= 4 && tc_type == 3 /*s16*/) {
                                    float scale = 1.0f / (float)(1 << tc_frac);
                                    const uint8_t* tcp = tc_data + tc_idx * tc_stride;
                                    verts[v].s = (float)j3d::reads16(tcp + 0) * scale;
                                    verts[v].t = (float)j3d::reads16(tcp + 2) * scale;
                                } else {
                                    verts[v].s = 0; verts[v].t = 0;
                                }
                                int yi = (int)(verts[v].y * 0.1f);
                                verts[v].r = (uint8_t)std::min(255, std::max(0, 80 + yi));
                                verts[v].g = (uint8_t)std::min(255, std::max(0, 140 + yi));
                                verts[v].b = (uint8_t)std::min(255, std::max(0, 60 + yi / 2));
                            }

                            // Convert strip/fan to triangles and draw
                            std::vector<Vtx> tris;
                            if (prim_type == 0x98) { // Triangle strip
                                for (uint16_t v = 2; v < vert_count; v++) {
                                    if (v & 1) { tris.push_back(verts[v-1]); tris.push_back(verts[v-2]); tris.push_back(verts[v]); }
                                    else       { tris.push_back(verts[v-2]); tris.push_back(verts[v-1]); tris.push_back(verts[v]); }
                                }
                            } else if (prim_type == 0xA0) { // Triangle fan
                                for (uint16_t v = 2; v < vert_count; v++) {
                                    tris.push_back(verts[0]); tris.push_back(verts[v-1]); tris.push_back(verts[v]);
                                }
                            } else if (prim_type == 0x90) { // Triangles
                                for (uint16_t v = 0; v + 2 < vert_count; v += 3) {
                                    tris.push_back(verts[v]); tris.push_back(verts[v+1]); tris.push_back(verts[v+2]);
                                }
                            }

                            if (!tris.empty()) {
                                uint32_t tc = (uint32_t)tris.size();
                                if (tc > 60000) tc = 60000;
                                GXBegin(GX_TRIANGLES, 0, tc);
                                for (uint32_t ti = 0; ti < tc; ti++) {
                                    GXPosition3f32(tris[ti].x, tris[ti].y, tris[ti].z);
                                    GXColor4u8(tris[ti].r, tris[ti].g, tris[ti].b, 255);
                                    if (tc_array && g_textures_loaded) {
                                        GXTexCoord2f32(tris[ti].s, tris[ti].t);
                                    }
                                    GXSubmitVertex();
                                }
                                GXEnd();
                            }
                        }
                    }
                }
            }
        }

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
