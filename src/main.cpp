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
#include "ww/jkr_archive.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <cmath>
#include <map>
#include <string>
#include <vector>

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

// Parsed BDL models for rendering
static j3d::J3DModel g_room_model;    // Island terrain
static j3d::J3DModel g_water_model;   // Ocean water
static j3d::J3DModel g_sky_model;     // Skybox
static bool g_room_model_loaded = false;
static bool g_water_model_loaded = false;
static bool g_sky_model_loaded = false;
static bool g_textures_loaded = false;
static bool g_water_textures_loaded = false;
static bool g_sky_textures_loaded = false;
static float g_camera_angle = 0.0f;
static gcrecomp::gx::GXTexObj g_tex_objs[8] = {};      // room textures
static gcrecomp::gx::GXTexObj g_water_tex_objs[9] = {}; // water textures
static gcrecomp::gx::GXTexObj g_sky_tex_objs[4] = {};   // sky textures

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

// Set during boot to the scene_proc created at startup (~line 1087).
// The HLE for func_80022CEC returns this so callers see the canonical
// scene already linked into sublayer1.listA and the exec queue, rather
// than a duplicate floating in Reality B. 0 until boot init runs.
static uint32_t g_boot_scene_proc = 0;

// Parsed actor entries from room.dzr ACTR chunk. Populated during room
// loading; consumed by the spawn-all loop when WW_SPAWN_TEST=all.
struct ActrEntry {
    char     name[9];
    uint32_t parameters;
    float    pos[3];
    int16_t  angle[3];
    uint16_t setID;
};
static std::vector<ActrEntry> g_actr_entries;

// World positions of successfully spawned actors. The render loop draws
// a colored cube marker at each so visible-actors progress can be seen
// without per-actor model resource loading.
struct SpawnedActorMarker {
    float    pos[3];
    uint16_t profname;
    uint32_t proc_addr;
};
static std::vector<SpawnedActorMarker> g_spawn_markers;

// ---- RARC buffer tracking (legacy, used for stage.dzs/BDL parsing) ----
static const uint32_t RARC_BUF_PTR_ADDR  = 0x817FFE00;
static const uint32_t RARC_BUF_SIZE_ADDR = 0x817FFE04;

// C-linkage host bump allocator. Called from the patched func_802412F8
// (cMl::memalignB) in recomp_0048.cpp via direct C++ call. The recompiled
// chain func_8003CFF0 → func_802412F8 → func_802B0494 (real JKR alloc)
// is unreachable in our environment because we never initialize the JKR
// heap structure; this override short-circuits to the bump arena.
// Diagnostic logger for the patched func_8003D788 (fpcEx_ToExecuteQ).
// Each time a process is linked into the dispatch queue, log its address
// + profile so we can see whether a new actor gets queued.
extern "C" void ww_log_to_executeq(uint32_t proc_addr) {
    static int s_log = 0;
    uint16_t profname = 0;
    if (proc_addr >= 0x80000000 && proc_addr < 0x81800000) {
        profname = g_mem.read16(proc_addr + 0x0E);
    }
    if (s_log < 300) {
        fprintf(stderr, "[ToExecQ] proc=0x%08X profname=0x%04X\n",
                proc_addr, profname);
        fflush(stderr);
        s_log++;
    }

    // Bridge to our boot dispatch list. The canonical fpcEx_ToLineQ inside
    // ToExecQ links the process into the parent process_node's line list,
    // but our per-frame iterator walks a separate list at 0x803BCD60+0x0C
    // (populated by boot insert_exec_list). Mirror the insertion here so
    // newly-spawned processes actually dispatch.
    //
    // Only active when WW_SPAWN_TEST is set, to avoid contaminating the
    // canonical dispatch chain during normal operation (the direct-spawn
    // path now bridges synchronously, so this hook is just belt-and-
    // suspenders).
    static const bool spawn_active = std::getenv("WW_SPAWN_TEST") != nullptr;
    if (!spawn_active) return;
    if (proc_addr < 0x80000000 || proc_addr >= 0x81800000) return;
    const uint32_t list_anchor = 0x803BCD60 + 0x0C;
    const uint32_t node_addr = proc_addr + 0x34;
    // Already linked? proc+0x34 anchor field at +4 == list_anchor means yes.
    if (g_mem.read32(node_addr + 4) == list_anchor) return;
    g_mem.write32(node_addr + 0, 0);            // prev = NULL
    g_mem.write32(node_addr + 4, list_anchor);  // anchor
    g_mem.write32(node_addr + 8, 0);            // next = NULL
    g_mem.write32(node_addr + 0xC, proc_addr);  // back-pointer
    uint32_t old_head = g_mem.read32(list_anchor);
    if (old_head == 0) {
        g_mem.write32(list_anchor,     node_addr);
        g_mem.write32(list_anchor + 4, node_addr);
        g_mem.write32(list_anchor + 8, 1);
    } else {
        uint32_t old_tail = g_mem.read32(list_anchor + 4);
        g_mem.write32(old_tail + 8, node_addr);
        g_mem.write32(node_addr + 0, old_tail);
        g_mem.write32(list_anchor + 4, node_addr);
        uint32_t count = g_mem.read32(list_anchor + 8);
        g_mem.write32(list_anchor + 8, count + 1);
    }
    if (s_log <= 30) {
        fprintf(stderr, "[ToExecQ]   bridged into boot exec list, count=%u\n",
                g_mem.read32(list_anchor + 8));
        fflush(stderr);
    }
}

// Diagnostic logger for the patched func_8003CA60 (fpcBs_Create entry).
extern "C" void ww_log_bs_create_enter(uint16_t profname, uint32_t procID) {
    static int s_log = 0;
    if (s_log < 200) {
        fprintf(stderr, "[bs_Create] profname=0x%04X procID=%u\n",
                profname, procID);
        fflush(stderr);
        s_log++;
    }
}
extern "C" void ww_log_bs_create_exit(uint16_t profname, uint32_t result) {
    static int s_log = 0;
    if (s_log < 200) {
        fprintf(stderr, "[bs_Create]   profname=0x%04X -> 0x%08X\n",
                profname, result);
        fflush(stderr);
        s_log++;
    }
}

// Diagnostic logger for the patched func_8003CF08 (fpcCtRq_Do). Bounded
// log so it doesn't spam past the first handful of invocations.
extern "C" void ww_log_ctrq_do(uint32_t req_addr, uint32_t phase_handler_addr) {
    static int s_log = 0;
    if (s_log < 200) {
        uint16_t procname = 0;
        if (req_addr >= 0x80000000 && req_addr < 0x81800000) {
            procname = g_mem.read16(req_addr + 0x50);
        }
        fprintf(stderr, "[ctRq_Do] req=0x%08X procname=0x%04X handler=0x%08X\n",
                req_addr, procname, phase_handler_addr);
        fflush(stderr);
        s_log++;
    }
}

// Used by recompiled patches (recomp_0006.cpp func_8003E2C8 and
// func_80040704) to skip their original bodies only when actor spawning
// is active. Cached to avoid env lookup per call.
extern "C" int ww_spawn_test_active() {
    static const int active = std::getenv("WW_SPAWN_TEST") ? 1 : 0;
    return active;
}

// Open the frame gate (func_8003EBD4) so fapGm_Execute can run dispatch
// without needing VBlank interrupt simulation. Only used in natural-boot
// mode where main() is driving its own loop.
extern "C" int ww_frame_gate_open() {
    static const int open_v = std::getenv("WW_FRAME_GATE_OPEN") ? 1 : 0;
    return open_v;
}

// Narrower gate: only redirect cMl::memalignB to our bump arena when
// we're actively calling fpcBs_Create from the direct-spawn path.
// Outside that window, leave canonical allocation behavior alone so
// the rest of the create-request pipeline returns NULL as before
// (otherwise it cascades into bad-address loops).
static std::atomic<int> g_alloc_override{0};
extern "C" int ww_alloc_override_active() {
    return g_alloc_override.load(std::memory_order_relaxed);
}
struct ScopedAllocOverride {
    ScopedAllocOverride()  { g_alloc_override.fetch_add(1, std::memory_order_relaxed); }
    ~ScopedAllocOverride() { g_alloc_override.fetch_sub(1, std::memory_order_relaxed); }
};

extern "C" uint32_t ww_bump_alloc_host(int32_t align, uint32_t size) {
    if (size == 0) return 0;
    if (align < 0) align = -align;
    if (align < 4) align = 4;
    uint32_t aligned = (g_bump_alloc_ptr + (uint32_t)align - 1) &
                       ~((uint32_t)align - 1);
    uint32_t end = aligned + size;
    if (end > BUMP_ALLOC_END) return 0;
    g_bump_alloc_ptr = end;
    return aligned;
}

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
    printf("  The Wind Waker - Static Recompilation\n");
    printf("  GameCube PowerPC 750 (Gekko) -> x86-64\n");
    printf("  Native Windows 11 / D3D11\n");
    printf("\n");
}

// Helper: render a J3D model with its textures through the GX pipeline
static void render_j3d_model(const j3d::J3DModel& model,
                             gcrecomp::gx::GXTexObj* tex_objs, bool& textures_loaded,
                             bool use_alpha = false) {
    using namespace gcrecomp::gx;

    // Find vertex arrays
    const j3d::VertexArray* pos_array = nullptr;
    const j3d::VertexArray* tc_array = nullptr;
    for (const auto& va : model.vertex_arrays) {
        if (va.attr == j3d::GX_VA_POS) pos_array = &va;
        if (va.attr == j3d::GX_VA_TEX0) tc_array = &va;
    }
    if (!pos_array || pos_array->count == 0 || pos_array->comp_type != 4) return;

    const uint8_t* pos_data = pos_array->data;
    uint32_t pos_count = pos_array->count;

    // Load textures once
    if (!textures_loaded && !model.textures.empty()) {
        textures_loaded = true;
        uint32_t ntex = std::min((uint32_t)model.textures.size(), 9u);
        for (uint32_t t = 0; t < ntex; t++) {
            const auto& th = model.textures[t];
            if (th.image_data && th.width > 0 && th.height > 0) {
                GXInitTexObj(&tex_objs[t], th.image_data,
                            th.width, th.height,
                            (GXTexFmt)th.format,
                            th.wrap_s, th.wrap_t, th.mipmap_count > 1);
            }
        }
    }

    // Vertex format
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (tc_array && textures_loaded) {
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    }

    uint32_t tc_count = tc_array ? tc_array->count : 0;
    const uint8_t* tc_data = tc_array ? tc_array->data : nullptr;
    uint32_t tc_stride = tc_array ? tc_array->stride : 0;
    uint32_t tc_type = tc_array ? tc_array->comp_type : 0;
    uint32_t tc_frac = tc_array ? tc_array->frac_bits : 0;

    for (uint32_t si = 0; si < model.shapes.size(); si++) {
        const auto& shape = model.shapes[si];

        // Bind texture for this batch based on material mapping
        int mat_idx = (si < model.shape_material.size()) ? model.shape_material[si] : (int)si;
        if (mat_idx < 0) mat_idx = (int)si;
        // Use material index as texture index (1:1 mapping)
        uint32_t tex_idx = (uint32_t)mat_idx;
        if (tex_idx >= model.textures.size()) tex_idx = 0;

        if (textures_loaded && !model.textures.empty()) {
            GXLoadTexObj(&tex_objs[tex_idx], 0);
            GXSetNumTevStages(1);
            GXSetTevOrder(GX_TEVSTAGE0, 0, 0, 0);
            GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
            GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
            GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
            GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
        } else {
            GXSetNumTevStages(1);
            GXSetTevOrder(GX_TEVSTAGE0, 0xFF, 0xFF, 0);
            GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
            GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
            GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
            GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, true, 0);
        }

        if (use_alpha) {
            GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, 0);
            GXSetZMode(true, GX_LEQUAL, false);  // read depth but don't write (water over terrain)
        } else {
            GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, 0);
            GXSetZMode(true, GX_LEQUAL, true);
        }

        for (const auto& pkt : shape.packets) {
            if (!pkt.display_list || pkt.display_list_size < 4) continue;
            const uint8_t* dl = pkt.display_list;
            uint32_t dl_end = pkt.display_list_size;
            uint32_t dp = 0;

            uint32_t bpv = 0;
            for (const auto& a : shape.attribs) {
                if (a.data_type == 1) bpv += 1;
                else if (a.data_type == 2) bpv += 1;
                else if (a.data_type == 3) bpv += 2;
            }
            if (bpv == 0) continue;

            while (dp < dl_end) {
                uint8_t cmd = dl[dp];
                if (cmd == 0) { dp++; continue; }
                if (cmd < 0x80) { dp++; continue; }

                uint8_t prim_type = cmd & 0xF8;
                if (dp + 3 > dl_end) break;
                uint16_t vert_count = j3d::read16(dl + dp + 1);
                dp += 3;
                if (vert_count == 0 || dp + vert_count * bpv > dl_end) break;

                struct Vtx { float x, y, z; uint8_t r, g, b, a; float s, t; };
                std::vector<Vtx> verts(vert_count);

                for (uint16_t v = 0; v < vert_count; v++) {
                    uint16_t pos_idx = 0, tc_idx2 = 0;
                    for (const auto& a : shape.attribs) {
                        uint16_t idx = 0;
                        if (a.data_type == 2) { idx = dl[dp]; dp += 1; }
                        else if (a.data_type == 3) { idx = j3d::read16(dl + dp); dp += 2; }
                        else if (a.data_type == 1) { dp += 1; continue; }
                        else continue;
                        if (a.attr == 9) pos_idx = idx;
                        if (a.attr == 13) tc_idx2 = idx;
                    }
                    if (pos_idx < pos_count) {
                        verts[v].x = j3d::readf32(pos_data + pos_idx * 12 + 0);
                        verts[v].y = j3d::readf32(pos_data + pos_idx * 12 + 4);
                        verts[v].z = j3d::readf32(pos_data + pos_idx * 12 + 8);
                    }
                    if (tc_data && tc_idx2 < tc_count && tc_stride >= 4 && tc_type == 3) {
                        float scale = 1.0f / (float)(1 << tc_frac);
                        const uint8_t* tcp = tc_data + tc_idx2 * tc_stride;
                        verts[v].s = (float)j3d::reads16(tcp + 0) * scale;
                        verts[v].t = (float)j3d::reads16(tcp + 2) * scale;
                    }
                    // Color by height: low=sand, mid=green, high=brown/rock
                    float h = verts[v].y;
                    if (use_alpha) {
                        // Water: blue tones
                        verts[v].r = 40; verts[v].g = 80; verts[v].b = 180;
                    } else if (h < 200.0f) {
                        // Low: sandy beach
                        verts[v].r = 210; verts[v].g = 190; verts[v].b = 130;
                    } else if (h < 1500.0f) {
                        // Mid: green vegetation
                        float t2 = (h - 200.0f) / 1300.0f;
                        verts[v].r = (uint8_t)(60 + 40 * t2);
                        verts[v].g = (uint8_t)(140 + 40 * t2);
                        verts[v].b = (uint8_t)(50 + 30 * t2);
                    } else {
                        // High: rocky
                        verts[v].r = 130; verts[v].g = 115; verts[v].b = 90;
                    }
                    verts[v].a = use_alpha ? 140 : 255;
                }

                // Convert to triangles
                std::vector<Vtx> tris;
                if (prim_type == 0x98) {
                    for (uint16_t v = 2; v < vert_count; v++) {
                        if (v & 1) { tris.push_back(verts[v-1]); tris.push_back(verts[v-2]); tris.push_back(verts[v]); }
                        else       { tris.push_back(verts[v-2]); tris.push_back(verts[v-1]); tris.push_back(verts[v]); }
                    }
                } else if (prim_type == 0xA0) {
                    for (uint16_t v = 2; v < vert_count; v++) {
                        tris.push_back(verts[0]); tris.push_back(verts[v-1]); tris.push_back(verts[v]);
                    }
                } else if (prim_type == 0x90) {
                    for (uint16_t v = 0; v + 2 < vert_count; v += 3) {
                        tris.push_back(verts[v]); tris.push_back(verts[v+1]); tris.push_back(verts[v+2]);
                    }
                }

                if (!tris.empty()) {
                    uint32_t tc2 = (uint32_t)tris.size();
                    if (tc2 > 60000) tc2 = 60000;
                    GXBegin(GX_TRIANGLES, 0, tc2);
                    for (uint32_t ti = 0; ti < tc2; ti++) {
                        GXPosition3f32(tris[ti].x, tris[ti].y, tris[ti].z);
                        GXColor4u8(tris[ti].r, tris[ti].g, tris[ti].b, tris[ti].a);
                        if (tc_array && textures_loaded) {
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

    // ---- func_80022CEC (dScnPly_c::create) ----
    // Now using the original recompiled code — the JKR mount system provides
    // proper getGlbResource/findVolume so the original creation chain can run.
    // The recompiled func_80022CEC in recomp_0003.cpp will:
    //   1. Get archive heap (func_80011AB4 → 0x80400010)
    //   2. Call getGlbResource → our JKR handler returns mounted archive
    //   3. Call findVolume → our JKR handler returns volume
    //   4. Call vtable[11] (detachResource) → no-op
    //   5. Call vtable[3] (unmount) → decrement refcount
    //   6. Framework setup + set scene created flag
    // ---- func_80022CEC (dScnPly_c::create) — HLE override ----
    // The original recompiled code has PPC stack leaks in its framework call
    // chain (func_8024019C → descriptor linking), corrupting func_80018430's
    // saved registers. We keep the native override for scene creation but the
    // JKR mount system is active for all OTHER getGlbResource/findVolume callers.
    g_func_table.register_func(0x80022CEC, [](PPCContext* ctx, Memory* mem) {
        static uint32_t next_proc_id = 0xD4;

        // func_80022CEC is the scene-specific creation function (likely
        // fopScnM_Create). It does NOT take a procname argument — it always
        // creates a scene-profile (0x0015) process. The create-request
        // pipeline calls fpcBs_Create (different function) for non-scene
        // profiles.

        // Reuse the canonical boot scene if available. Avoids the dual-reality
        // problem where our parallel-allocated scene was never linked into
        // sublayer1.listA or the exec queue, so the framework dispatch never
        // saw it. The boot scene is already in both lists. Gated by env var
        // so we can A/B test against the prior allocate-fresh behavior.
        static const bool reuse_boot_scene =
            std::getenv("WW_REUSE_BOOT_SCENE") != nullptr;
        if (reuse_boot_scene && g_boot_scene_proc != 0) {
            // Signal "scene created" flag (caller checks this).
            mem->write32(ctx->r[13] - 30492, 1);
            static int sLogCount = 0;
            if (sLogCount++ < 3) {
                fprintf(stderr, "[SCN] Reusing boot scene_proc 0x%08X "
                                "(dispatched via sublayer1.listA + exec queue)\n",
                        g_boot_scene_proc);
                fflush(stderr);
            }

            // Drive the per-profile create_method on the canonical (linked)
            // scene. Boot init only wrote raw header fields — it never ran
            // the profile's create method, so per-scene state (kankyo, actor
            // lists, etc.) hasn't been initialized. Now that the proc is
            // properly in the dispatch lists, any state the create_method
            // sets up is reachable from the per-frame execute_method.
            static const bool drive_create_on_reuse =
                std::getenv("WW_DRIVE_CREATE_METHOD") != nullptr;
            static bool drove_create = false;
            if (drive_create_on_reuse && !drove_create) {
                drove_create = true;
                uint32_t methods_ptr = mem->read32(g_boot_scene_proc + 0xA8);
                uint32_t create_method = 0;
                if (methods_ptr >= 0x80000000 && methods_ptr < 0x817FF000) {
                    create_method = mem->read32(methods_ptr + 0x00);
                }
                if (create_method >= 0x80000000 && create_method < 0x817FF000) {
                    // Temporarily set init_state=0 so fpcNd_Create runs its
                    // layer-init branch on the boot scene.
                    uint32_t saved_0C = mem->read32(g_boot_scene_proc + 0x0C);
                    mem->write32(g_boot_scene_proc + 0x0C,
                                 (saved_0C & 0x0000FFFFu) /* keep profname */);
                    uint32_t saved_lr = ctx->lr;
                    ctx->r[3] = g_boot_scene_proc;
                    fprintf(stderr,
                            "[SCN] Driving create_method 0x%08X on boot "
                            "scene 0x%08X\n",
                            create_method, g_boot_scene_proc);
                    fflush(stderr);
                    g_func_table.call(create_method, ctx, mem);
                    uint32_t phase = ctx->r[3];
                    ctx->lr = saved_lr;
                    fprintf(stderr,
                            "[SCN] boot-scene create_method phase=%u "
                            "(4=COMPLEATE)\n",
                            phase);
                    fflush(stderr);
                    // Restore init_state=2 (executing) regardless of phase
                    // so the next-frame dispatch still picks it up.
                    mem->write32(g_boot_scene_proc + 0x0C,
                                 0x02020000u | (saved_0C & 0xFFFFu));
                }
            }

            ctx->r[3] = g_boot_scene_proc;
            return;
        }

        // Allocate 0xF8 bytes (profile 0x0015 object)
        uint32_t size = 0xF8;
        uint32_t aligned = (g_bump_alloc_ptr + 31) & ~31;
        if (aligned + size > BUMP_ALLOC_END) {
            ctx->r[3] = 0;
            return;
        }
        uint32_t proc_addr = aligned;
        g_bump_alloc_ptr = aligned + size;
        memset(mem->translate(proc_addr), 0, size);

        // Initialize process header from Dolphin reference
        mem->write32(proc_addr + 0x00, 0x09130001);
        mem->write32(proc_addr + 0x04, next_proc_id++);
        mem->write32(proc_addr + 0x08, 0x00150000);
        // +0x0C: state (init_state s8, create_phase u8) | profname (s16)
        // Canonical fpcBs_Create sets init_state=0. When driving create_method
        // (env var below), use 0 so fpcNd_Create's `if (init_state == 0)`
        // branch runs and initializes the sublayer's priority lists. Without
        // that branch the scene has no place for child actors.
        // We bump init_state back to 2 after the create_method completes.
        static const bool init_zero =
            std::getenv("WW_DRIVE_CREATE_METHOD") != nullptr;
        mem->write32(proc_addr + 0x0C, init_zero ? 0x00000015u : 0x02020015u);
        mem->write32(proc_addr + 0x10, 0x80391B88);
        mem->write32(proc_addr + 0x24, proc_addr);
        mem->write32(proc_addr + 0x28, 0x01000000);
        mem->write32(proc_addr + 0x40, proc_addr);
        mem->write32(proc_addr + 0x44, 0x01000000);
        mem->write32(proc_addr + 0x48, 0x00000001);
        mem->write32(proc_addr + 0x58, proc_addr);
        mem->write32(proc_addr + 0x74, proc_addr);
        mem->write32(proc_addr + 0x90, 0x8003FD40);
        mem->write32(proc_addr + 0x98, 0xFFFFFFFD);
        mem->write32(proc_addr + 0x9C, 0x0001FFFD);
        mem->write32(proc_addr + 0xA0, 0xFFFFFFFD);
        mem->write32(proc_addr + 0xA4, 0x0001FFFD);
        mem->write32(proc_addr + 0xA8, 0x803726E8);   // process_method_class*
        mem->write32(proc_addr + 0xB4, 0x09130003);
        mem->write32(proc_addr + 0xB8, 0x80372178);
        mem->write32(proc_addr + 0xBC, 0x00000002);
        mem->write32(proc_addr + 0xC0, 0x09130004);
        mem->write32(proc_addr + 0xC8, 0x803B9E98);
        mem->write32(proc_addr + 0xD0, proc_addr);
        mem->write32(proc_addr + 0xD4, 0x01000000);
        mem->write32(proc_addr + 0xD8, 0x80391B74);
        mem->write32(proc_addr + 0xFC, 0x000002D4);

        mem->write32(ctx->r[13] - 30492, 1);

        // Canonical pipeline: phase_SubCreateProcess calls fpcBs_SubCreate
        // which dispatches the profile's create_method. Our HLE short-circuits
        // the whole pipeline; without this call the per-profile init never
        // runs (no actor lists, no kankyo setup, etc.). Drive it here.
        //
        // process_method_class layout (from f_pc_method.h):
        //   +0x00 create_method   +0x04 delete_method
        //   +0x08 execute_method  +0x0C is_delete_method
        //
        // Gate behind an env var so we can A/B test against the prior
        // behavior if this introduces crashes.
        if (init_zero) {
            uint32_t methods_ptr = mem->read32(proc_addr + 0xA8);
            uint32_t create_method = 0;
            if (methods_ptr >= 0x80000000 && methods_ptr < 0x817FF000) {
                create_method = mem->read32(methods_ptr + 0x00);
            }
            if (create_method >= 0x80000000 && create_method < 0x817FF000) {
                // Save caller's r3 (return value slot); restore proc_addr after.
                uint32_t saved_lr = ctx->lr;
                ctx->r[3] = proc_addr;
                fprintf(stderr,
                        "[SCN] Calling create_method 0x%08X for proc 0x%08X "
                        "(init_state=0)\n",
                        create_method, proc_addr);
                fflush(stderr);
                g_func_table.call(create_method, ctx, mem);
                uint32_t phase = ctx->r[3];
                ctx->lr = saved_lr;
                fprintf(stderr,
                        "[SCN] create_method returned phase=%u "
                        "(0=INIT 1=LOADING 2=NEXT 3=UNK3 4=COMPLEATE 5=ERROR)\n",
                        phase);
                fflush(stderr);

                // Per fpcBs_SubCreate: on NEXT_e/COMPLEATE_e the pipeline
                // advances; fpcEx_ToExecuteQ eventually bumps init_state=2.
                // We short-circuit by promoting init_state to 2 directly so
                // the per-frame fpcEx_Handler dispatches execute_method.
                uint8_t new_init = (phase == 2 /*NEXT*/ ||
                                    phase == 4 /*COMPLEATE*/) ? 2 : 1;
                uint8_t new_phase =
                    (phase == 2 || phase == 4) ? 2 /*NEXT*/ : 0 /*INIT*/;
                mem->write32(proc_addr + 0x0C,
                             (uint32_t(new_init) << 24) |
                             (uint32_t(new_phase) << 16) | 0x0015u);
            } else {
                fprintf(stderr,
                        "[SCN] No valid create_method (methods=0x%08X, fn=0x%08X)\n",
                        methods_ptr, create_method);
                // No create driven — restore the pre-existing executing state.
                mem->write32(proc_addr + 0x0C, 0x02020015);
            }
        }

        ctx->r[3] = proc_addr;

        fprintf(stderr, "[SCN] Scene process created at 0x%08X (id=%u)\n",
                proc_addr, next_proc_id - 1);
    });
    printf("[*] func_80022CEC: HLE override (JKR mount active for other callers)\n");

    // ---- Missing small functions not discovered by recompiler ----
    // These are comparison functions between func_8004003C and func_80040080
    // that the CFG builder missed. They're used as search predicates by the
    // framework's linked list iteration.

    // func_80040050: compare [r3+8] (halfword) with [r4] (halfword)
    // Returns r3 if equal, 0 if not
    g_func_table.register_func(0x80040050, [](PPCContext* ctx, Memory* mem) {
        int16_t a = (int16_t)mem->read16(ctx->r[3] + 8);
        int16_t b = (int16_t)mem->read16(ctx->r[4]);
        if (a != b) ctx->r[3] = 0;
    });

    // func_80040068: compare [r3+4] (word, unsigned) with [r4] (word)
    // Returns r3 if equal, 0 if not
    g_func_table.register_func(0x80040068, [](PPCContext* ctx, Memory* mem) {
        uint32_t a = mem->read32(ctx->r[3] + 4);
        uint32_t b = mem->read32(ctx->r[4]);
        if (a != b) ctx->r[3] = 0;
    });

    // Per-process execute callback (via CALL_INDIRECT from layer iterator).
    g_func_table.register_func(0x80040198, [](PPCContext* ctx, Memory* mem) {
        static int trace_count = 0;
        uint32_t node = ctx->r[3];
        uint32_t proc_base = (node >= 0x80000000) ? mem->read32(node + 12) : 0;
        uint32_t callback_holder = ctx->r[4];
        uint32_t callback_fn = (callback_holder >= 0x80000000)
            ? mem->read32(callback_holder) : 0;
        if (trace_count < 40) {
            // profname is at base_process_class +0x0E (s16).
            uint16_t profname = (proc_base >= 0x80000000)
                ? mem->read16(proc_base + 0x0E) : 0;
            const char* kind = (callback_fn == 0x8003E370) ? "EXEC"
                             : (callback_fn == 0x8003E390) ? "DRAW"
                             : "????";
            fprintf(stderr, "[%s] proc=0x%08X profname=0x%04X cb=0x%08X\n",
                    kind, proc_base, profname, callback_fn);
            fflush(stderr);
        }
        trace_count++;

        // Execute the real callback chain
        extern void func_8003D96C(PPCContext* ctx, Memory* mem);
        extern void func_8003D964(PPCContext* ctx, Memory* mem);
        extern void func_8024560C(PPCContext* ctx, Memory* mem);

        uint32_t r28 = ctx->r[3];
        uint32_t r29 = ctx->r[4];
        ctx->r[3] = mem->read32(node + 12);
        uint32_t r31 = mem->read32(ctx->r[3] + 44);

        func_8003D96C(ctx, mem);
        uint32_t r30 = ctx->r[3];
        ctx->r[3] = r31;
        func_8003D964(ctx, mem);
        ctx->r[3] = r28;
        ctx->r[4] = r29;
        func_8024560C(ctx, mem);
        uint32_t result = ctx->r[3];
        ctx->r[3] = r30;
        func_8003D964(ctx, mem);
        ctx->r[3] = result;
    });
    // PPCHalt (0x8030150C): infinite spin loop → return immediately
    g_func_table.register_func(0x8030150C, idle_loop_replacement);
    // OSReport (0x802CB8D0): debug printf that may write to HW console → no-op
    g_func_table.register_func(0x802CB8D0, [](PPCContext* ctx, Memory* mem) {});
    // Scene manager execute (0x802558CC) — called per-frame via vtable.
    // Needs timing globals at r13(-26600) not yet initialized. No-op for now.
    g_func_table.register_func(0x802558CC, [](PPCContext* ctx, Memory* mem) {});
    // func_8000AF2C (mDoGph_gInf_c execute) — the scene manager's per-frame
    // Trace: actual profile-specific execute methods (called via CALL_INDIRECT)
    // Scene: 0x801948B4 (dScnPly_c::execute), Actor: 0x8015D87C (env execute)
    // No-op placeholder (strcmp trace removed — direct bl bypasses func_table)
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

        // cMl::memalignB (func_802412F8): r3=align, r4=size.
        // Reads sCurrentHeap from r13-28096 — which is uninitialized in
        // our boot, causing fpcCtRq_Create to fail allocator. Redirect
        // here to use our bump arena directly.
        g_func_table.register_func(0x802412F8, [](PPCContext* ctx, Memory* mem) {
            int32_t align = (int32_t)ctx->r[3];
            uint32_t size = ctx->r[4];
            static int s_log = 0;
            if (s_log < 10) {
                fprintf(stderr, "[memalignB] align=%d size=%u bump=0x%08X\n",
                        align, size, g_bump_alloc_ptr);
                fflush(stderr);
                s_log++;
            }
            if (size == 0) { ctx->r[3] = 0; return; }
            if (align < 0) align = -align;
            if (align < 4) align = 4;
            uint32_t aligned = (g_bump_alloc_ptr + (uint32_t)align - 1) &
                               ~((uint32_t)align - 1);
            uint32_t end = aligned + size;
            if (end > BUMP_ALLOC_END) { ctx->r[3] = 0; return; }
            g_bump_alloc_ptr = end;
            ctx->r[3] = aligned;
            if (s_log <= 10) {
                fprintf(stderr, "[memalignB]   -> 0x%08X\n", aligned);
                fflush(stderr);
            }
        });

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

        // Initialize JKR archive mount system (vtable + sVolumeList)
        jkr::init(g_func_table, g_mem);
        jkr::register_os_funcs(g_func_table, g_mem);
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

    // ---- Create scene manager and timing structure manually ----
    // mDoGph_Create normally creates these, but depends on GX/camera init.
    {
        // Scene manager
        uint32_t mgr_addr = 0x817FFA00;
        memset(g_mem.translate(mgr_addr), 0, 64);
        g_mem.write32(mgr_addr + 0, 0x80395C20);    // vtable
        g_mem.write32(mgr_addr + 12, 0xFFFFFFFF);   // uninitialized sentinel
        g_mem.write32(mgr_addr + 28, 1);             // init flag
        g_mem.write32(g_ctx.r[13] - 27984, mgr_addr);
        printf("[*]   Scene manager at 0x%08X\n", mgr_addr);

        // Timing/display structure (from Dolphin capture at 0x805F4EC0)
        // Contains screen dimensions, frame state, and vtable pointer.
        // The scene manager stores a pointer to this at +4, and process
        // dispatch reads it via r13(-30792).
        uint32_t timing_addr = 0x817FF900;
        memset(g_mem.translate(timing_addr), 0, 64);
        g_mem.write32(timing_addr + 0x00, 0x8039D578);   // vtable (DOL data)
        g_mem.write32(timing_addr + 0x04, 0x00000001);   // frame state
        g_mem.write32(timing_addr + 0x08, 0x001A001A);   // ticks per frame (26, 26)
        // Float: 640.0 (GC render width)
        { float f = 640.0f; uint32_t v; memcpy(&v, &f, 4);
          g_mem.write32(timing_addr + 0x18, (v >> 24) | ((v >> 8) & 0xFF00) |
                        ((v << 8) & 0xFF0000) | (v << 24)); }
        // Actually, memory is big-endian. write32 handles this.
        // 640.0 in big-endian = 0x44200000
        g_mem.write32(timing_addr + 0x18, 0x44200000);   // 640.0 (width)
        g_mem.write32(timing_addr + 0x1C, 0x43F00000);   // 480.0 (height)
        g_mem.write32(timing_addr + 0x20, 0xFFFFFFFF);   // mask

        // Point r13(-30792) to timing structure
        g_mem.write32(g_ctx.r[13] - 30792, timing_addr);
        // Also set scene_mgr+4 to point to it
        g_mem.write32(mgr_addr + 4, timing_addr);

        // Set timing-related SDA globals that reference our structures
        // (These were heap pointers in Dolphin, skipped during bulk load)
        g_mem.write32(g_ctx.r[13] - 26600, timing_addr + 0x40);  // timing obj
        // r13(-32592) = timing source for per-process dispatch
        // In Dolphin this is 0xFFFFFFFF — already loaded from Dolphin SDA dump

        printf("[*]   Timing structure at 0x%08X (640x480, vtable=0x8039D578)\n", timing_addr);
    }

    printf("[*]   fapGm_Create (game framework)...\n"); fflush(stdout);
    func_80023218(&g_ctx, &g_mem);
    printf("[*]   fapGm_Create done.\n"); fflush(stdout);
    // Watchpoint: track when creation queue item gets corrupted
    auto dump_item = [&](const char* label) {
        uint32_t item = g_mem.read32(0x803A72C0 + 36); // queue head
        if (item >= 0x80000000 && item < 0x82000000) {
            fprintf(stderr, "[WATCH] %s: item+20=0x%08X (expect 0x80022CEC)\n",
                    label, g_mem.read32(item + 20));
        }
        fflush(stderr);
    };
    dump_item("after fapGm_Create");

    printf("[*]   Framework post-init (func_80022DF8)...\n"); fflush(stdout);
    func_80022DF8(&g_ctx, &g_mem);
    printf("[*] Game framework initialized.\n");
    dump_item("after post-init");

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

    // ---- Populate process tree from Dolphin reference data ----
    // The framework's process tree uses sublayer priority lists, not the flat
    // tree at 0x803726A0. Processes live inside sublayers, which are part of
    // the root layer at 0x80372690. The creation chain (func_80022CEC) can't
    // create processes due to JKR dependencies, so we manually construct the
    // essential process objects using field values captured from Dolphin.
    //
    // Hierarchy (from Dolphin capture during gameplay):
    //   Root layer (0x80372690) → sublayer list at +0x4C (0x803726DC)
    //     └ Sublayer 0 (0x803BCE20, DOL static) → listA at +0x38
    //         └ Root layer process (profile 0x0007, 0x1D0 bytes)
    //             └ Contains embedded sublayer 1 at process+0xBC
    //                 └ Scene process (profile 0x0015, 0xF8 bytes) in listA
    //
    // List node structure (embedded at process+0x18 for listA):
    //   +0x00: prev_node (NULL for head)
    //   +0x04: list_anchor (sublayer + list_offset)
    //   +0x08: next_node (NULL for tail)
    //   +0x0C: process_base_addr
    // ---- Forced-boot block ----
    // Manual process-tree population using Dolphin-captured field values.
    // Forces the game into "active gameplay on sea_T" state without going
    // through the natural boot sequence (NDEV check → company logos →
    // title → menu → play).
    //
    // Skip this when WW_NATURAL_BOOT=1 so main()/fapGm_Execute can drive
    // the scene progression itself (target: get to the title screen).
    bool natural_boot = std::getenv("WW_NATURAL_BOOT") != nullptr;
    if (natural_boot) {
        printf("[*] WW_NATURAL_BOOT=1 — skipping forced process tree.\n");
    } else {
    printf("[*] Populating process tree...\n");
    {
        // --- Helper: insert a process into a sublayer's listA (+0x38) ---
        auto insert_listA = [](Memory* mem, uint32_t sublayer, uint32_t proc_addr) {
            uint32_t list_anchor = sublayer + 0x38;
            uint32_t node_addr = proc_addr + 0x18;  // listA node at +0x18
            // Set up node: {prev=0, list_anchor, next=0, proc_base}
            mem->write32(node_addr + 0, 0);           // prev = NULL (head)
            mem->write32(node_addr + 4, list_anchor);  // anchor
            mem->write32(node_addr + 8, 0);           // next = NULL (tail)
            mem->write32(node_addr + 0xC, proc_addr); // back-pointer to process
            // Update sublayer list: head, tail, count
            uint32_t old_head = mem->read32(list_anchor);
            if (old_head == 0) {
                mem->write32(list_anchor, node_addr);      // head
                mem->write32(list_anchor + 4, node_addr);  // tail
                mem->write32(list_anchor + 8, 1);          // count
            } else {
                // Append: new node becomes new tail
                uint32_t old_tail = mem->read32(list_anchor + 4);
                mem->write32(old_tail + 8, node_addr);     // old_tail.next = new
                mem->write32(node_addr + 0, old_tail);     // new.prev = old_tail
                mem->write32(list_anchor + 4, node_addr);  // list.tail = new
                uint32_t count = mem->read32(list_anchor + 8);
                mem->write32(list_anchor + 8, count + 1);
            }
        };

        // --- 1. Create root layer process (profile 0x0007, size 0x1D0) ---
        const uint32_t ROOT_PROC_SIZE = 0x1D0;
        uint32_t rp_aligned = (g_bump_alloc_ptr + 31) & ~31;
        uint32_t root_proc = rp_aligned;
        g_bump_alloc_ptr = rp_aligned + ROOT_PROC_SIZE;
        memset(g_mem.translate(root_proc), 0, ROOT_PROC_SIZE);

        // Process header
        g_mem.write32(root_proc + 0x00, 0x09130001);  // magic
        g_mem.write32(root_proc + 0x04, 0x000000D3);  // process ID
        g_mem.write32(root_proc + 0x08, 0x00070000);  // profile 0x0007
        g_mem.write32(root_proc + 0x0C, 0x02020007);  // flags | profile
        g_mem.write32(root_proc + 0x10, 0x80394BC4);  // profile descriptor
        // Self-referential pointers
        g_mem.write32(root_proc + 0x24, root_proc);
        g_mem.write32(root_proc + 0x28, 0x01000000);
        g_mem.write32(root_proc + 0x2C, 0x803BCE20);  // parent sublayer 0
        g_mem.write32(root_proc + 0x30, 0x0001FFFD);
        g_mem.write32(root_proc + 0x40, root_proc);
        g_mem.write32(root_proc + 0x44, 0x01000000);
        g_mem.write32(root_proc + 0x48, 0x00000001);
        g_mem.write32(root_proc + 0x58, root_proc);
        g_mem.write32(root_proc + 0x74, root_proc);
        // Dispatch/vtable fields
        g_mem.write32(root_proc + 0x90, 0x8003FD40);  // dispatch function
        g_mem.write32(root_proc + 0x9C, 0x0001FFFD);
        g_mem.write32(root_proc + 0xA4, 0x0001FFFD);
        g_mem.write32(root_proc + 0xA8, 0x80372720);  // function table
        g_mem.write32(root_proc + 0xB4, 0x09130002);  // sub-state

        // Embedded sublayer 1 starts at root_proc+0xBC
        uint32_t sublayer1 = root_proc + 0xBC;
        g_mem.write32(sublayer1 + 0x00, 0x803BCE20);   // prev = sublayer 0
        g_mem.write32(sublayer1 + 0x04, 0x803726DC);   // parent = root sublayer list
        g_mem.write32(sublayer1 + 0x08, 0);            // next = NULL (we only create 1 sublayer)
        g_mem.write32(sublayer1 + 0x0C, 0x00000007);
        g_mem.write32(root_proc + 0xCC, root_proc + 0xD0);  // node info ptr
        g_mem.write32(root_proc + 0xD0, 0x00000010);
        g_mem.write32(root_proc + 0xD4, root_proc);    // back-ref

        // Root process vtable/dispatch table (from Dolphin)
        g_mem.write32(root_proc + 0xB8, 0x803720E8);  // dispatch vtable
        g_mem.write32(root_proc + 0x1AC, 0x80394BB0); // profile vtable
        g_mem.write32(root_proc + 0x1B4, 0x80372150); // another vtable
        g_mem.write32(root_proc + 0x1BC, root_proc);
        g_mem.write32(root_proc + 0x1C0, 0x01000000);

        // Insert root process into sublayer 0's listA
        insert_listA(&g_mem, 0x803BCE20, root_proc);

        // Link sublayer 1 into root layer's sublayer list
        // Sublayer 0 is head. Make sublayer 1 the next after sublayer 0.
        uint32_t sl0 = 0x803BCE20;
        g_mem.write32(sl0 + 0x08, sublayer1);          // sl0.next = sublayer1
        // Update root sublayer list tail and count
        g_mem.write32(0x803726E0, sublayer1);           // tail = sublayer1
        g_mem.write32(0x803726E4, 2);                   // count = 2

        printf("[*]   Root process at 0x%08X (profile 0x0007)\n", root_proc);
        printf("[*]   Embedded sublayer 1 at 0x%08X\n", sublayer1);

        // --- 2. Create scene process (profile 0x0015, size 0xF8) ---
        const uint32_t SCENE_PROC_SIZE = 0xF8;
        uint32_t sp_aligned = (g_bump_alloc_ptr + 31) & ~31;
        uint32_t scene_proc = sp_aligned;
        g_bump_alloc_ptr = sp_aligned + SCENE_PROC_SIZE;
        memset(g_mem.translate(scene_proc), 0, SCENE_PROC_SIZE);

        // Process header
        g_mem.write32(scene_proc + 0x00, 0x09130001);
        g_mem.write32(scene_proc + 0x04, 0x000000D4);  // process ID
        g_mem.write32(scene_proc + 0x08, 0x00150000);   // profile 0x0015
        g_mem.write32(scene_proc + 0x0C, 0x02020015);
        g_mem.write32(scene_proc + 0x10, 0x80391B88);   // profile descriptor
        // Self-referential
        g_mem.write32(scene_proc + 0x24, scene_proc);
        g_mem.write32(scene_proc + 0x28, 0x01000000);
        g_mem.write32(scene_proc + 0x2C, sublayer1);     // parent = sublayer 1
        g_mem.write32(scene_proc + 0x30, 0x00010000);
        g_mem.write32(scene_proc + 0x34, root_proc + 0xFC);  // ref to root
        g_mem.write32(scene_proc + 0x38, 0x803BCD6C);
        g_mem.write32(scene_proc + 0x40, scene_proc);
        g_mem.write32(scene_proc + 0x44, 0x01000000);
        g_mem.write32(scene_proc + 0x48, 0x00000001);
        g_mem.write32(scene_proc + 0x58, scene_proc);
        g_mem.write32(scene_proc + 0x74, scene_proc);
        // Dispatch/state
        g_mem.write32(scene_proc + 0x90, 0x8003FD40);
        g_mem.write32(scene_proc + 0x98, 0xFFFFFFFD);
        g_mem.write32(scene_proc + 0x9C, 0x0001FFFD);
        g_mem.write32(scene_proc + 0xA0, 0xFFFFFFFD);
        g_mem.write32(scene_proc + 0xA4, 0x0001FFFD);
        g_mem.write32(scene_proc + 0xA8, 0x803726E8);   // function table
        g_mem.write32(scene_proc + 0xB4, 0x09130003);
        g_mem.write32(scene_proc + 0xB8, 0x80372178);   // dispatch vtable
        g_mem.write32(scene_proc + 0xBC, 0x00000002);
        g_mem.write32(scene_proc + 0xC0, 0x09130004);
        g_mem.write32(scene_proc + 0xC8, 0x803B9E98);
        g_mem.write32(scene_proc + 0xD0, scene_proc);
        g_mem.write32(scene_proc + 0xD4, 0x01000000);
        g_mem.write32(scene_proc + 0xD8, 0x80391B74);   // sub-descriptor
        g_mem.write32(scene_proc + 0xFC, 0x000002D4);

        // Insert scene process into sublayer 1's listA
        insert_listA(&g_mem, sublayer1, scene_proc);

        // Publish for the func_80022CEC HLE so it returns this canonical,
        // already-linked scene instead of allocating a parallel duplicate.
        g_boot_scene_proc = scene_proc;

        printf("[*]   Scene process at 0x%08X (profile 0x0015)\n", scene_proc);

        // --- 3. Verify sublayer structure ---
        uint32_t sl0_listA_head = g_mem.read32(0x803BCE20 + 0x38);
        uint32_t sl0_listA_count = g_mem.read32(0x803BCE20 + 0x40);
        uint32_t sl1_listA_head = g_mem.read32(sublayer1 + 0x38);
        uint32_t sl1_listA_count = g_mem.read32(sublayer1 + 0x40);
        printf("[*]   Sublayer 0 listA: head=0x%08X count=%u\n", sl0_listA_head, sl0_listA_count);
        printf("[*]   Sublayer 1 listA: head=0x%08X count=%u\n", sl1_listA_head, sl1_listA_count);

        // --- 4. Populate the per-frame execution queue at 0x803BCD60 ---
        // func_80040200 (called from fapGm_Execute) reads r13(-32608) as the
        // root iteration data pointer. This points to 0x803BCD60, which contains
        // multiple priority lists (head/tail/count at 12-byte intervals).
        // Processes link into this via a node at process+0x34.
        //
        // From Dolphin: root proc (0x80AC10C8+0x34) and scene proc (0x80ABE8E8+0x34)
        // are both in list[1] at offset +0x0C (anchor = 0x803BCD6C).
        const uint32_t EXEC_QUEUE = 0x803BCD60;

        // Helper: insert process into execution queue list
        auto insert_exec_list = [](Memory* mem, uint32_t list_anchor, uint32_t proc_addr) {
            uint32_t node_addr = proc_addr + 0x34;  // exec queue node at +0x34
            mem->write32(node_addr + 0, 0);           // prev = NULL
            mem->write32(node_addr + 4, list_anchor); // anchor
            mem->write32(node_addr + 8, 0);           // next = NULL
            mem->write32(node_addr + 0xC, proc_addr); // back-pointer
            // Update list
            uint32_t old_head = mem->read32(list_anchor);
            if (old_head == 0) {
                mem->write32(list_anchor, node_addr);     // head
                mem->write32(list_anchor + 4, node_addr); // tail
                mem->write32(list_anchor + 8, 1);         // count
            } else {
                uint32_t old_tail = mem->read32(list_anchor + 4);
                mem->write32(old_tail + 8, node_addr);    // old_tail.next = new
                mem->write32(node_addr + 0, old_tail);    // new.prev = old_tail
                mem->write32(list_anchor + 4, node_addr); // list.tail = new
                uint32_t count = mem->read32(list_anchor + 8);
                mem->write32(list_anchor + 8, count + 1);
            }
        };

        // Insert root process into exec queue list[1] (offset +0x0C)
        insert_exec_list(&g_mem, EXEC_QUEUE + 0x0C, root_proc);
        // Insert scene process into exec queue list[1]
        insert_exec_list(&g_mem, EXEC_QUEUE + 0x0C, scene_proc);

        // --- 5. Generic helper: create process and add to exec queue ---
        // All process headers share the same layout (see Process Object Layout).
        // We initialize the essential fields and link into exec queue list[1].
        static uint32_t s_next_proc_id = 0xD5;
        auto create_process = [&](uint16_t profile_id, uint32_t size, uint32_t desc_ptr,
                                  uint32_t fn_table, uint32_t parent_sublayer) -> uint32_t {
            uint32_t aligned = (g_bump_alloc_ptr + 31) & ~31;
            if (aligned + size > BUMP_ALLOC_END) return 0;
            uint32_t proc = aligned;
            g_bump_alloc_ptr = aligned + size;
            memset(g_mem.translate(proc), 0, size);

            // Process header
            g_mem.write32(proc + 0x00, 0x09130001);           // magic
            g_mem.write32(proc + 0x04, s_next_proc_id++);     // process ID
            g_mem.write32(proc + 0x08, (uint32_t)profile_id << 16);
            g_mem.write32(proc + 0x0C, 0x02020000 | profile_id);
            g_mem.write32(proc + 0x10, desc_ptr);             // profile descriptor
            // Self-referential pointers
            g_mem.write32(proc + 0x24, proc);
            g_mem.write32(proc + 0x28, 0x01000000);
            g_mem.write32(proc + 0x2C, parent_sublayer);
            g_mem.write32(proc + 0x40, proc);
            g_mem.write32(proc + 0x44, 0x01000000);
            g_mem.write32(proc + 0x48, 0x00000001);
            g_mem.write32(proc + 0x58, proc);
            g_mem.write32(proc + 0x74, proc);
            // Dispatch
            g_mem.write32(proc + 0x90, 0x8003FD40);           // dispatch function
            g_mem.write32(proc + 0xA8, fn_table);             // function table 1 (from desc+0x0C)
            g_mem.write32(proc + 0xB8, g_mem.read32(desc_ptr + 0x1C)); // function table 2 (from desc+0x1C)
            // Profile-specific fields from descriptor
            g_mem.write32(proc + 0x98, g_mem.read32(desc_ptr + 0));   // priority base
            g_mem.write32(proc + 0x9C, g_mem.read32(desc_ptr + 4));   // parent priority
            g_mem.write32(proc + 0xA0, g_mem.read32(desc_ptr + 0));
            g_mem.write32(proc + 0xA4, g_mem.read32(desc_ptr + 4));

            // Insert into exec queue list[1]
            insert_exec_list(&g_mem, EXEC_QUEUE + 0x0C, proc);

            return proc;
        };

        // --- 6. Create essential processes from Dolphin reference ---
        // These are the core processes from sublayer 1 that drive the game.
        // Profile: desc_ptr, size, fn_table (from profile desc +0x0C)
        struct ProcTemplate {
            uint16_t profile;
            uint32_t size;
            uint32_t desc_ptr;
            uint32_t fn_table;  // from desc+0x0C or Dolphin capture
        };
        const ProcTemplate essential_procs[] = {
            // Room manager (manages room loading)
            {0x0017, 0x100,  0x80391254, 0x803726E8},
            // Environment handler
            {0x0028, 0x298,  0x80390718, 0x803726E8},
            // Camera (large, but needed for view)
            {0x00A9, 0x4C28, 0x8038FD8C, 0x803726E8},
            // Scene sub-processes
            {0x01B5, 0x698,  0x80389620, 0x803726E8},
            {0x01BA, 0x2A0,  0x80390860, 0x803726E8},
            {0x01BB, 0x2AC,  0x803908B0, 0x803726E8},
            // Timing/controller
            {0x01BC, 0x2E0,  0x80389D80, 0x803726E8},
        };

        // Map profile → allocated address for template loading
        std::vector<std::pair<uint16_t, uint32_t>> proc_addrs;
        for (const auto& tmpl : essential_procs) {
            uint32_t proc = create_process(tmpl.profile, tmpl.size, tmpl.desc_ptr,
                                           tmpl.fn_table, sublayer1);
            if (proc) {
                proc_addrs.push_back({tmpl.profile, proc});
                printf("[*]   Process 0x%04X at 0x%08X (%u bytes)\n",
                       tmpl.profile, proc, tmpl.size);
            }
        }

        // --- Load process object templates from Dolphin captures ---
        // Each dolphin_proc_XXXX.bin contains the runtime state of a process
        // object from Dolphin. We copy scalar (non-pointer) fields to provide
        // internal state that the execute methods depend on.
        // Skip: header fields (+0x00 to +0xBF already set), heap pointers,
        // and self-referential pointers.
        auto load_proc_template = [](Memory* mem, uint32_t proc_addr, uint32_t proc_size,
                                     const char* filename) -> int {
            FILE* f = fopen(filename, "rb");
            if (!f) return 0;
            std::vector<uint8_t> data(proc_size);
            size_t n = fread(data.data(), 1, proc_size, f);
            fclose(f);
            if (n < proc_size) return 0;

            int copied = 0;
            // Start from +0xBC (after header/dispatch fields we already set)
            // For the first 0xBC bytes, only copy fields we DIDN'T set
            for (size_t off = 0; off + 3 < proc_size; off += 4) {
                uint32_t val = (data[off] << 24) | (data[off+1] << 16) |
                               (data[off+2] << 8) | data[off+3];
                if (val == 0) continue;
                // Skip ALL pointer-like values (0x80000000-0x81FFFFFF)
                // These reference Dolphin's memory layout, not ours
                if (val >= 0x80000000 && val < 0x82000000) continue;
                // Skip magic/state markers
                if ((val & 0xFFFF0000) == 0x09130000) continue;
                // Skip header fields we already set
                if (off < 0x14) continue;
                // Don't overwrite values we already set
                uint32_t existing = mem->read32(proc_addr + (uint32_t)off);
                if (existing != 0) continue;

                mem->write32(proc_addr + (uint32_t)off, val);
                copied++;
            }
            return copied;
        };

        // Load templates for all essential processes
        for (const auto& [profile, addr] : proc_addrs) {
            char fname[64];
            snprintf(fname, sizeof(fname), "dolphin_proc_%04X.bin", profile);
            uint32_t size = 0;
            for (const auto& t : essential_procs)
                if (t.profile == profile) { size = t.size; break; }
            int n = load_proc_template(&g_mem, addr, size, fname);
            if (n > 0) {
                printf("[*]   Template 0x%04X: %d values loaded\n", profile, n);
            }
        }
        // Also load templates for root and scene processes
        {
            int n1 = load_proc_template(&g_mem, root_proc, ROOT_PROC_SIZE, "dolphin_proc_0007.bin");
            int n2 = load_proc_template(&g_mem, scene_proc, SCENE_PROC_SIZE, "dolphin_proc_0015.bin");
            if (n1) printf("[*]   Template 0x0007 (root): %d values loaded\n", n1);
            if (n2) printf("[*]   Template 0x0015 (scene): %d values loaded\n", n2);
        }

        // Set r13(-32608) = exec queue base, r13(-32604) = number of lists
        g_mem.write32(g_ctx.r[13] - 32608, EXEC_QUEUE);
        g_mem.write32(g_ctx.r[13] - 32604, 16);  // 16 priority lists

        uint32_t total_procs = g_mem.read32(EXEC_QUEUE + 0x0C + 8);
        printf("[*]   Exec queue: %u processes in list[1]\n", total_procs);
        // --- 7. Initialize game info stage name for scene state machine ---
        // func_801942E0 compares stage name at 0x803C9D2C with "sea" (0x8035EF58).
        // If they don't match, the entire scene execute is skipped.
        // 0x803C9D2C = 0x803C4BF8 (dComIfG_gameInfo base) + 0x5134 (play.mStage offset)
        {
            uint32_t stage_name_addr = 0x803C9D2C;
            // The compare is against "sea" (area name), not "sea_T" (full stage)
            g_mem.write8(stage_name_addr + 0, 's');
            g_mem.write8(stage_name_addr + 1, 'e');
            g_mem.write8(stage_name_addr + 2, 'a');
            g_mem.write8(stage_name_addr + 3, 0);
            printf("[*]   Stage name at 0x%08X = \"sea\"\n", stage_name_addr);

        }

        printf("[*] Process tree populated.\n");
    }
    }  // end !natural_boot
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
        dump_item("before scene change");
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

            dump_item("before Stage.arc load");
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
                                    // Mount into JKR system
                                    jkr::mount("Stage", buf1_addr, decomp_size, g_mem);
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
            dump_item("after Stage.arc decompress");
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
                    dump_item("after DVD callback");
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

        dump_item("before Room44 load");
        printf("[*] Loading Room44.arc (%u bytes, %uKB) from ISO...\n",
               ROOM44_ARC_SIZE, ROOM44_ARC_SIZE / 1024);

        static std::vector<uint8_t> room_compressed(ROOM44_ARC_SIZE);
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
                                // Mount into JKR system
                                jkr::mount("Room44", room_buf_addr, room_decomp_size, g_mem);
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
                // Allocate at the END of the arena to avoid overlapping game allocations.
                // The game uses the bump allocator from the bottom up; we use the top down.
                uint32_t room_size = (uint32_t)room_compressed.size();
                uint32_t room_buf_addr = (BUMP_ALLOC_END - room_size) & ~31;
                if (room_buf_addr > g_bump_alloc_ptr) {

                    uint8_t* room_dst = g_mem.translate(room_buf_addr);
                    memcpy(room_dst, room_compressed.data(), room_size);
                    printf("[*]   Copied %u bytes (uncompressed) → 0x%08X\n",
                           room_size, room_buf_addr);
                    dump_item("after Room44 copy to emulated RAM");

                    if (gcrecomp::rarc_is_archive(room_dst, room_size)) {
                        gcrecomp::RARCArchive room_archive;
                        if (gcrecomp::rarc_parse(room_dst, room_size, room_archive)) {
                            printf("[*]   Room44.arc RARC contents (%zu files):\n",
                                   room_archive.files.size());
                            for (const auto& f : room_archive.files) {
                                printf("[*]     %s (%u bytes)\n",
                                       f.path.c_str(), f.data_size);
                            }
                            // Mount into JKR system
                            jkr::mount("Room44", room_buf_addr, room_size, g_mem);
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
                                    // Keep models for rendering
                                    if (strcmp(*name, "bdl/model.bdl") == 0) {
                                        g_room_model = std::move(model);
                                        g_room_model_loaded = true;
                                        printf("[*]   Stored room model for rendering.\n");
                                    } else if (strcmp(*name, "bdl/model1.bdl") == 0) {
                                        g_water_model = std::move(model);
                                        g_water_model_loaded = true;
                                        printf("[*]   Stored water model for rendering.\n");
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
                                            if (strcmp(*name, "bdl/vr_sky.bdl") == 0) {
                                                g_sky_model = std::move(model);
                                                g_sky_model_loaded = true;
                                                printf("[*]   Stored skybox model for rendering.\n");
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

        // ---- Parse room.dzr from Room44.arc (ACTR/SCOB chunks) ----
        // Format: u32 chunk_count, then chunk_count headers of
        // { char tag[4]; u32 entry_count; u32 entry_offset; }
        // ACTR entries are 0x20 bytes each: char name[8], u32 params,
        // float pos[3], s16 angle[3], u16 setID.
        if (!room_compressed.empty()) {
            const uint8_t* room_raw = room_compressed.data();
            uint32_t room_raw_size = (uint32_t)room_compressed.size();
            gcrecomp::RARCArchive room_arc;
            if (gcrecomp::rarc_is_archive(room_raw, room_raw_size) &&
                gcrecomp::rarc_parse(room_raw, room_raw_size, room_arc))
            {
                const gcrecomp::RARCFile* dzr =
                    room_arc.find_path("dzr/room.dzr");
                if (dzr) {
                    const uint8_t* dz = room_arc.file_data(
                        *dzr, room_raw, room_raw_size);
                    if (dz && dzr->data_size >= 4) {
                        auto be32 = [](const uint8_t* p) {
                            return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
                                 | (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
                        };
                        uint32_t chunk_count = be32(dz);
                        printf("[*] room.dzr: %u bytes, %u chunks\n",
                               dzr->data_size, chunk_count);
                        for (uint32_t i = 0;
                             i < chunk_count && 4 + i * 12 + 12 <= dzr->data_size;
                             ++i)
                        {
                            const uint8_t* ch = dz + 4 + i * 12;
                            char tag[5] = {(char)ch[0], (char)ch[1],
                                           (char)ch[2], (char)ch[3], 0};
                            uint32_t cnt = be32(ch + 4);
                            uint32_t off = be32(ch + 8);
                            printf("[*]   '%s': %u entries @ 0x%X\n",
                                   tag, cnt, off);

                            // Parse ACTR entries (also TGSC, TGDR etc share
                            // the 0x20-byte base layout starting with name).
                            // Store all entries to a global for later
                            // spawn-all processing.
                            if (strcmp(tag, "ACTR") == 0 && cnt > 0
                                && off + cnt * 0x20 <= dzr->data_size)
                            {
                                g_actr_entries.clear();
                                g_actr_entries.reserve(cnt);
                                for (uint32_t j = 0; j < cnt; ++j) {
                                    const uint8_t* e = dz + off + j * 0x20;
                                    ActrEntry a = {};
                                    memcpy(a.name, e, 8);
                                    a.parameters = be32(e + 8);
                                    auto bef = [&](int p) {
                                        uint32_t b = be32(e + p);
                                        float f;
                                        memcpy(&f, &b, 4);
                                        return f;
                                    };
                                    a.pos[0] = bef(0x0C);
                                    a.pos[1] = bef(0x10);
                                    a.pos[2] = bef(0x14);
                                    a.angle[0] = (int16_t)((e[0x18] << 8) | e[0x19]);
                                    a.angle[1] = (int16_t)((e[0x1A] << 8) | e[0x1B]);
                                    a.angle[2] = (int16_t)((e[0x1C] << 8) | e[0x1D]);
                                    a.setID = (uint16_t)((e[0x1E] << 8) | e[0x1F]);
                                    g_actr_entries.push_back(a);
                                }
                                int show = std::min<uint32_t>(cnt, 10);
                                for (int j = 0; j < show; ++j) {
                                    const auto& a = g_actr_entries[j];
                                    printf("[ACTR] %-8s parm=0x%08X pos=(%.0f,%.0f,%.0f) ay=%d set=0x%04X\n",
                                           a.name, a.parameters,
                                           a.pos[0], a.pos[1], a.pos[2],
                                           a.angle[1], a.setID);
                                }
                                if ((uint32_t)show < cnt) {
                                    printf("[ACTR] ... (%u more, %zu stored)\n",
                                           cnt - show, g_actr_entries.size());
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    fflush(stdout);

    // ---- Locate WW's actor name table (dStage_objectNameInf array) ----
    // Each entry is exactly 12 bytes:
    //   char name[8]       (inline, padded with zeros)
    //   s16  procname      (profile ID, big-endian in guest memory)
    //   s8   argument
    //   (1 byte padding)
    // The table is a contiguous array of these in the DOL data section.
    // Address established empirically as 0x80372818 (see earlier scan
    // output). The scan-based fallback only runs if WW_RESCAN_ACTR_TABLE
    // is set — it's millions of g_mem.read8 calls in debug, ~10s.
    {
        const char* probes[] = {
            "woodbx", "ikada_h", "swood5", "swood3", "swood1",
            "Tree", "Link", "NpcSo", "Vds", nullptr
        };
        uint32_t candidate_table = 0x80372818;  // hardcoded after verification
        if (std::getenv("WW_RESCAN_ACTR_TABLE")) {
        printf("[ACTR-TBL] Scanning for dStage_objectNameInf table "
               "(12-byte stride)...\n");
        candidate_table = 0;
        int hits = 0;
        // Iterate at 4-byte alignment over the data range.
        for (uint32_t scan = 0x80300000; scan < 0x80400000 && !candidate_table;
             scan += 4)
        {
            // Check if any probe matches an 8-byte inline name here.
            for (int p = 0; probes[p]; ++p) {
                size_t pl = strlen(probes[p]);
                if (pl > 7) continue;
                bool name_match = true;
                for (size_t k = 0; k < 8; ++k) {
                    uint8_t b = g_mem.read8(scan + (uint32_t)k);
                    uint8_t want = (k < pl) ? (uint8_t)probes[p][k] : 0;
                    if (b != want) { name_match = false; break; }
                }
                if (!name_match) continue;

                // Plausibility: profile at +8 is BE u16 in 0x0001..0x0FFF,
                // arg at +0xA is small signed (-1..127).
                uint16_t proc = (uint16_t)((g_mem.read8(scan + 8) << 8) |
                                            g_mem.read8(scan + 9));
                int8_t arg = (int8_t)g_mem.read8(scan + 0x0A);
                if (proc == 0 || proc > 0x0FFF) continue;

                printf("[ACTR-TBL]   '%s' @0x%08X proc=0x%04X arg=%d\n",
                       probes[p], scan, proc, arg);

                if (++hits >= 1) {
                    // Walk backward to find table start (look for zero/invalid
                    // entries or alignment break).
                    uint32_t guess = scan;
                    // Try backing off by 12-byte multiples until we hit a
                    // name byte 0 OR a name with non-printable first char.
                    for (int back = 1; back <= 200; ++back) {
                        uint32_t e = scan - (uint32_t)(back * 12);
                        uint8_t first = g_mem.read8(e);
                        if (first == 0 || first < 0x20 || first > 0x7E) {
                            guess = scan - (uint32_t)((back - 1) * 12);
                            break;
                        }
                    }
                    candidate_table = guess;
                }
                if (hits >= 6) break;
            }
        }
        }  // end WW_RESCAN_ACTR_TABLE

        if (candidate_table) {
            printf("[ACTR-TBL] Table candidate base: 0x%08X. Dumping 16 "
                   "entries:\n", candidate_table);
            for (int i = 0; i < 16; ++i) {
                uint32_t e = candidate_table + (uint32_t)(i * 12);
                char nm[9] = {};
                for (int k = 0; k < 8; ++k) {
                    nm[k] = (char)g_mem.read8(e + (uint32_t)k);
                }
                uint16_t proc = (uint16_t)((g_mem.read8(e + 8) << 8) |
                                            g_mem.read8(e + 9));
                int8_t arg = (int8_t)g_mem.read8(e + 0x0A);
                printf("[ACTR-TBL]   [%2d] '%-8.8s' proc=0x%04X arg=%d\n",
                       i, nm, proc, arg);
            }
        } else {
            printf("[ACTR-TBL] No candidate table found. Names may be in a "
                   "REL or compressed section.\n");
        }
        fflush(stdout);
    }

    // ---- Dump the fpcSCtRq phase handler array ----
    // Array of 7 cPhs__Handler at 0x803727FC (from fpcSCtRq_Request body).
    // Layout: [phase_Load, phase_CreateProcess, phase_SubCreateProcess,
    //         phase_IsComplete, phase_PostMethod, phase_Done, NULL]
    {
        const char* names[7] = {
            "phase_Load", "phase_CreateProcess", "phase_SubCreateProcess",
            "phase_IsComplete", "phase_PostMethod", "phase_Done", "NULL"
        };
        printf("[SCTRQ] phase handler array @ 0x803727FC:\n");
        for (int i = 0; i < 7; ++i) {
            uint32_t addr = g_mem.read32(0x803727FC + (uint32_t)(i * 4));
            printf("[SCTRQ]   [%d] %-24s = 0x%08X\n", i, names[i], addr);
        }
    }

    // ---- Marker-only mode: WW_MARK_ACTRS=1 ----
    // Skip the actual fpcBs_Create spawn pipeline; just push visible
    // markers for every parsed ACTR entry. Lets us render all 172
    // markers without the per-actor execute_method CPU cost.
    if (std::getenv("WW_MARK_ACTRS") != nullptr) {
        for (const auto& a : g_actr_entries) {
            SpawnedActorMarker m{};
            m.pos[0] = a.pos[0];
            m.pos[1] = a.pos[1];
            m.pos[2] = a.pos[2];
            // Use first 2 chars of name as a pseudo-profname for color hash.
            m.profname = (uint16_t)((uint8_t)a.name[0] << 8 | (uint8_t)a.name[1]);
            g_spawn_markers.push_back(m);
        }
        printf("[MARK-ACTRS] Pushed %zu markers from room.dzr ACTR entries\n",
               g_actr_entries.size());
    }

    // ---- Spawn test: enqueue actor(s) via fpcSCtRq_Request ----
    // WW_SPAWN_TEST=1   → one hard-coded woodbx (profile 0x010C)
    // WW_SPAWN_TEST=all → walk g_actr_entries, look each name up in the
    //                     actor table at 0x80372818, spawn each.
    if (const char* spawn_mode = std::getenv("WW_SPAWN_TEST")) {
        const uint32_t ACTR_TBL = 0x80372818;
        // Helper: look up an 8-char name (truncated/padded) in the actor
        // name table. Returns 0 if not found.
        auto lookup_proc = [&](const char* nm8) -> uint16_t {
            for (uint32_t e = ACTR_TBL; e < 0x80380000; e += 12) {
                // Stop at zero/non-printable first byte (end of table).
                uint8_t first = g_mem.read8(e);
                if (first == 0 || first < 0x20 || first > 0x7E) break;
                bool match = true;
                for (int k = 0; k < 8; ++k) {
                    uint8_t got = g_mem.read8(e + (uint32_t)k);
                    uint8_t want = (uint8_t)(nm8[k] ? nm8[k] : 0);
                    if (got != want) { match = false; break; }
                }
                if (match) {
                    return (uint16_t)((g_mem.read8(e + 8) << 8) |
                                       g_mem.read8(e + 9));
                }
            }
            return 0;
        };

        // Allocate one prm_class per spawn. Helper builds it from an ActrEntry.
        auto write_be_f = [&](uint32_t addr, float v) {
            uint32_t bits; memcpy(&bits, &v, 4);
            g_mem.write32(addr, bits);
        };
        auto build_prm = [&](const ActrEntry& a) -> uint32_t {
            uint32_t prm_aligned = (g_bump_alloc_ptr + 31) & ~31;
            uint32_t prm = prm_aligned;
            g_bump_alloc_ptr = prm_aligned + 0x24;
            memset(g_mem.translate(prm), 0, 0x24);
            g_mem.write32(prm + 0x00, a.parameters);
            write_be_f(prm + 0x04, a.pos[0]);
            write_be_f(prm + 0x08, a.pos[1]);
            write_be_f(prm + 0x0C, a.pos[2]);
            g_mem.write16(prm + 0x10, (uint16_t)a.angle[0]);
            g_mem.write16(prm + 0x12, (uint16_t)a.angle[1]);
            g_mem.write16(prm + 0x14, (uint16_t)a.angle[2]);
            g_mem.write16(prm + 0x16, a.setID);
            g_mem.write8 (prm + 0x18, 10);
            g_mem.write8 (prm + 0x19, 10);
            g_mem.write8 (prm + 0x1A, 10);
            g_mem.write32(prm + 0x1C, 0xFFFFFFFE);  // parent_id NONE
            g_mem.write8 (prm + 0x20, (uint8_t)-1);
            g_mem.write8 (prm + 0x21, 44);
            return prm;
        };

        // Direct-spawn helper: bypass the fpcSCtRq queue entirely (its
        // iterator stops after the first request completes — likely a
        // delete-during-iteration bug). Call fpcBs_Create (func_8003CA60)
        // directly to allocate+init the process, then bridge it into our
        // boot exec queue ourselves.
        static uint32_t s_actor_id_counter = 0x100;
        auto direct_spawn = [&](const ActrEntry& a, uint16_t procname,
                                uint32_t prm) -> uint32_t {
            // Activate the cMl::memalignB override only for this call so
            // the canonical create-request pipeline (which also calls
            // memalignB) doesn't get its NULL-return behavior changed.
            ScopedAllocOverride alloc_guard;
            g_ctx.r[3] = procname;
            g_ctx.r[4] = s_actor_id_counter++;
            g_ctx.r[5] = prm;
            g_func_table.call(0x8003CA60, &g_ctx, &g_mem);  // fpcBs_Create
            uint32_t proc = g_ctx.r[3];
            if (proc == 0 || proc < 0x80000000 || proc >= 0x81800000) {
                return 0;
            }
            // Remember this actor's world position so the render loop
            // can draw a marker for it.
            SpawnedActorMarker m{};
            m.pos[0] = a.pos[0];
            m.pos[1] = a.pos[1];
            m.pos[2] = a.pos[2];
            m.profname = procname;
            m.proc_addr = proc;
            g_spawn_markers.push_back(m);
            // Set init_state = 2 (executing) so the dispatcher accepts it.
            uint32_t s0C = g_mem.read32(proc + 0x0C);
            g_mem.write32(proc + 0x0C, (s0C & 0x0000FFFFu) | 0x02020000u);
            // Bridge into boot exec queue (same code as ww_log_to_executeq).
            const uint32_t list_anchor = 0x803BCD60 + 0x0C;
            const uint32_t node_addr = proc + 0x34;
            if (g_mem.read32(node_addr + 4) != list_anchor) {
                g_mem.write32(node_addr + 0, 0);
                g_mem.write32(node_addr + 4, list_anchor);
                g_mem.write32(node_addr + 8, 0);
                g_mem.write32(node_addr + 0xC, proc);
                uint32_t old_head = g_mem.read32(list_anchor);
                if (old_head == 0) {
                    g_mem.write32(list_anchor,     node_addr);
                    g_mem.write32(list_anchor + 4, node_addr);
                    g_mem.write32(list_anchor + 8, 1);
                } else {
                    uint32_t old_tail = g_mem.read32(list_anchor + 4);
                    g_mem.write32(old_tail + 8, node_addr);
                    g_mem.write32(node_addr + 0, old_tail);
                    g_mem.write32(list_anchor + 4, node_addr);
                    uint32_t count = g_mem.read32(list_anchor + 8);
                    g_mem.write32(list_anchor + 8, count + 1);
                }
            }
            return proc;
        };

        auto enqueue_one = [&](const ActrEntry& a, uint16_t procname) -> uint32_t {
            return direct_spawn(a, procname, build_prm(a));
        };

        if (strcmp(spawn_mode, "all") == 0) {
            if (g_actr_entries.empty()) {
                printf("[SPAWN-ALL] No ACTR entries parsed; nothing to spawn.\n");
            } else {
                // Cap via WW_SPAWN_LIMIT to avoid CPU-hogging the game
                // thread with N actor execute_methods that read unset state.
                size_t limit = g_actr_entries.size();
                if (const char* lim_env = std::getenv("WW_SPAWN_LIMIT")) {
                    size_t lv = (size_t)strtoul(lim_env, nullptr, 10);
                    if (lv && lv < limit) limit = lv;
                }
                printf("[SPAWN-ALL] Resolving %zu actor names against table "
                       "0x%08X and enqueuing (limit=%zu)...\n",
                       g_actr_entries.size(), ACTR_TBL, limit);
                int ok = 0, miss = 0, fail = 0;
                std::map<std::string, int> miss_names;
                for (size_t i = 0; i < limit; ++i) {
                    const auto& a = g_actr_entries[i];
                    uint16_t pn = lookup_proc(a.name);
                    if (pn == 0) {
                        miss++;
                        miss_names[a.name]++;
                        continue;
                    }
                    uint32_t proc = enqueue_one(a, pn);
                    if (proc == 0) fail++; else ok++;
                }
                printf("[SPAWN-ALL] Result: %d enqueued, %d name-misses, "
                       "%d enqueue-failures (of %zu attempted, %zu total in room).\n",
                       ok, miss, fail, limit, g_actr_entries.size());
                if (!miss_names.empty()) {
                    printf("[SPAWN-ALL] Missing names (top):\n");
                    int shown = 0;
                    for (const auto& kv : miss_names) {
                        if (shown++ >= 10) break;
                        printf("[SPAWN-ALL]   '%s' x%d\n", kv.first.c_str(),
                               kv.second);
                    }
                }
            }
        } else {
            // Single-actor smoke test (woodbx hardcoded position).
            printf("[SPAWN-TEST] Single woodbx (profile 0x010C)...\n");
            ActrEntry a = {};
            strcpy(a.name, "woodbx");
            a.pos[0] = -203985.0f; a.pos[1] = 544.0f; a.pos[2] = 316656.0f;
            a.angle[2] = -6189; a.setID = 0xFFFF;
            uint32_t proc = enqueue_one(a, 0x010C);
            printf("[SPAWN-TEST]   proc=0x%08X\n", proc);
        }
        fflush(stdout);
    }

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

    // ---- Load Dolphin reference state into game memory ----
    // The scene state machine and process execute methods read from large BSS
    // structures that are zeroed in our recomp but have meaningful state in the
    // real game. We load captured state from Dolphin binary dumps to provide
    // the expected runtime context.
    //
    // Skipped under WW_NATURAL_BOOT — those captures are from active sea_T
    // gameplay and would re-force us into that state, defeating natural boot.
    if (!natural_boot) {
        struct RegionLoad {
            const char* filename;
            uint32_t addr;
            uint32_t size;
            bool skip_heap_ptrs;  // If true, don't copy values that look like heap pointers
        };
        const RegionLoad loads[] = {
            // Game info (dComIfG_gameInfo) — scene state, event flags, stage data
            {"dolphin_game_info.bin", 0x803C4BF8, 0x6000, true},
            // Scene/environment runtime info
            {"dolphin_scene_info.bin", 0x803E4AB4, 0x1000, true},
            // SDA globals — framework state, timing, counters
            {"dolphin_sda_globals.bin", 0x803F60E0, 0x1A70, true},
        };

        int total_loaded = 0;
        for (const auto& load : loads) {
            FILE* f = fopen(load.filename, "rb");
            if (!f) {
                printf("[*] Dolphin state %s not found (skipping)\n", load.filename);
                continue;
            }
            fseek(f, 0, SEEK_END);
            size_t fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            size_t to_read = (fsize < load.size) ? fsize : load.size;

            std::vector<uint8_t> data(to_read);
            fread(data.data(), 1, to_read, f);
            fclose(f);

            // Copy to emulated memory, optionally skipping heap pointers
            uint8_t* dst = g_mem.translate(load.addr);
            int copied = 0, skipped = 0;
            for (size_t off = 0; off + 3 < to_read; off += 4) {
                uint32_t val = (data[off] << 24) | (data[off+1] << 16) |
                               (data[off+2] << 8) | data[off+3];
                if (val == 0) continue;  // Skip zeros (already zeroed)

                // Skip heap pointers (0x80400000+) — they point to Dolphin's
                // allocations, not ours
                if (load.skip_heap_ptrs && val >= 0x80400000 && val < 0x81800000) {
                    skipped++;
                    continue;
                }

                // Don't overwrite values we specifically set (exec queue, heaps, etc.)
                uint32_t target_addr = load.addr + (uint32_t)off;
                uint32_t existing = g_mem.read32(target_addr);
                if (existing != 0) {
                    skipped++;
                    continue;  // Don't overwrite our initialized values
                }

                dst[off+0] = data[off+0];
                dst[off+1] = data[off+1];
                dst[off+2] = data[off+2];
                dst[off+3] = data[off+3];
                copied++;
            }
            printf("[*] Loaded %s: %d values copied, %d skipped\n",
                   load.filename, copied, skipped);
            total_loaded += copied;
        }
        printf("[*] Dolphin state loaded: %d total values\n", total_loaded);
    }

    // Set scene loading state to 0 (active/running).
    // Skip under WW_NATURAL_BOOT so the scene_mgr drives its own state
    // transitions (boot → NDEV → company logos → title → menu → play).
    if (!natural_boot) {
        g_mem.write16(g_ctx.r[13] - 30754, 0);
        printf("[*] Scene state set to 0 (active).\n");
    } else {
        printf("[*] WW_NATURAL_BOOT — leaving scene state alone.\n");

        // Trigger canonical boot scene creation. func_80022DF8 is what
        // main01 calls after the mDo*_Create init. It calls
        // func_800183B4(0x80022CEC, 0) which queues a scene-create request
        // pointing at our HLE'd scene-create function. The per-frame
        // fapGm_Execute should then process the request and produce a
        // scene process for the dispatcher.
        printf("[*] WW_NATURAL_BOOT — calling func_80022DF8 to trigger "
               "boot scene create...\n");
        extern void func_80022DF8(PPCContext* ctx, Memory* mem);
        func_80022DF8(&g_ctx, &g_mem);
        printf("[*] func_80022DF8 returned; root scene ptr (r13-30488) = "
               "0x%08X\n", g_mem.read32(g_ctx.r[13] - 30488));
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

            // Pump framework creation queue — process ONE pending request per frame.
            // The original game has a background thread. We process one at a time
            // to avoid infinite loops when create functions add new requests.
            {
                const uint32_t CREATE_Q = 0x803A72C0;
                uint32_t pending_count = g_mem.read32(CREATE_Q + 44);
                if (pending_count > 0) {
                    uint32_t item_addr = g_mem.read32(CREATE_Q + 36);
                    uint32_t sentinel = CREATE_Q + 36;
                    // Find the first unprocessed item
                    while (item_addr != 0 && item_addr != sentinel) {
                        uint8_t created = g_mem.read8(item_addr + 12);
                        if (created == 0) {
                            if (frame <= 20) {
                                uint32_t create_fn = g_mem.read32(item_addr + 20);
                                fprintf(stderr, "[FW] BEFORE: item=0x%X fn=0x%08X +16=0x%08X +24=0x%08X\n",
                                        item_addr, create_fn,
                                        g_mem.read32(item_addr + 16),
                                        g_mem.read32(item_addr + 24));
                                fflush(stderr);
                            }
                            // Save/restore the item data to protect from corruption
                            uint8_t item_backup[32];
                            memcpy(item_backup, g_mem.translate(item_addr), 32);

                            g_ctx.r[3] = item_addr;
                            func_80018430(&g_ctx, &g_mem);
                            if (frame <= 20) {
                                // Check if item was corrupted
                                bool corrupted = memcmp(item_backup + 16, g_mem.translate(item_addr) + 16, 8) != 0;
                                fprintf(stderr, "[FW] AFTER: item+12=%u item+28=0x%08X +20=0x%08X sp=0x%08X %s\n",
                                        g_mem.read8(item_addr + 12),
                                        g_mem.read32(item_addr + 28),
                                        g_mem.read32(item_addr + 20),
                                        g_ctx.r[1],
                                        corrupted ? "CORRUPTED!" : "ok");
                                if (corrupted) {
                                    // Restore vtable and create_fn, keep result
                                    memcpy(g_mem.translate(item_addr) + 16, item_backup + 16, 8);
                                    fprintf(stderr, "[FW] Restored item+16/+20 from backup\n");
                                }
                                fflush(stderr);
                            }
                            break; // Only one per frame
                        }
                        item_addr = g_mem.read32(item_addr + 4);
                    }
                }
            }

            // Check if new creation requests were added
            if (frame <= 3) {
                fprintf(stderr, "[FW] frame=%d\n", frame);
                // Dump descriptor list entries
                uint32_t dh = g_mem.read32(g_ctx.r[13] - 28120);
                int di = 0;
                while (dh != 0 && dh >= 0x80000000 && dh < 0x82000000 && di < 5) {
                    uint16_t d0 = g_mem.read16(dh);
                    uint16_t d2 = g_mem.read16(dh + 2);
                    uint32_t d12 = g_mem.read32(dh + 12);
                    uint32_t d28 = g_mem.read32(dh + 28);
                    fprintf(stderr, "[FW]   desc[%d] @0x%X: +0=%u +2=%u +12=0x%X +28=0x%X\n",
                            di, dh, d0, d2, d12, d28);
                    dh = g_mem.read32(dh + 8); // next
                    di++;
                }
                fflush(stderr);
            }
            if (frame <= 5) {
                const uint32_t CREATE_Q = 0x803A72C0;
                uint32_t post_count = g_mem.read32(CREATE_Q + 44);
                uint32_t post_head = g_mem.read32(CREATE_Q + 36);
                uint32_t sl0_n = g_mem.read32(0x803BCE20 + 0x40);
                uint32_t desc_head = g_mem.read32(g_ctx.r[13] - 28120);
                uint32_t desc_tail = g_mem.read32(g_ctx.r[13] - 28116);
                fprintf(stderr, "[FW] Post-pump: q_count=%u q_head=0x%X sl0=%u desc_head=0x%X desc_tail=0x%X\n",
                        post_count, post_head, sl0_n, desc_head, desc_tail);
                fflush(stderr);
            }

            // Call the process descriptor iterator directly to bootstrap
            // process creation. Normally this runs from a root process in the
            // tree, but the tree is empty (chicken-and-egg). Calling it here
            // processes profile descriptors and creates process instances.
            {
                extern void func_802403E0(PPCContext* ctx, Memory* mem);
                func_802403E0(&g_ctx, &g_mem);
            }

            // Check scene state machine inputs
            if (frame <= 2) {
                char s1[8] = {}, s2[8] = {};
                uint8_t* p1 = g_mem.translate(0x803C9D2C);
                uint8_t* p2 = g_mem.translate(0x8035EF58);
                for (int i = 0; i < 7 && p1[i]; i++) s1[i] = p1[i];
                for (int i = 0; i < 7 && p2[i]; i++) s2[i] = p2[i];
                fprintf(stderr, "[SCENE] strcmp: \"%s\" vs \"%s\" = %d\n",
                        s1, s2, strcmp(s1, s2));
                fflush(stderr);
            }

            // ---- Per-frame dispatch ----
            // Two modes:
            //   Forced-boot (default): bypass frame gate + call sub-funcs
            //     directly in the canonical order. Lets us drive dispatch
            //     even though the canonical fapGm_Execute would return early
            //     at the frame-gate check.
            //   Natural boot (WW_NATURAL_BOOT=1): call fapGm_Execute itself
            //     (func_800231E4) and let main()/scene_mgr drive everything.
            //     The frame gate may still block; if so we'll see and react.
            if (natural_boot) {
                func_800231E4(&g_ctx, &g_mem);    // fapGm_Execute — natural
            } else {
                extern void func_802539A4(PPCContext* ctx, Memory* mem);  // pre-frame setup
                extern void func_8024135C(PPCContext* ctx, Memory* mem);  // descriptor init
                extern void func_8003D314(PPCContext* ctx, Memory* mem);  // delete queue
                extern void func_8003FF00(PPCContext* ctx, Memory* mem);  // birth queue → tree
                extern void func_8003D150(PPCContext* ctx, Memory* mem);  // process queue
                extern void func_8003D7E0(PPCContext* ctx, Memory* mem);  // EXECUTE dispatch
                extern void func_802449AC(PPCContext* ctx, Memory* mem);  // frame counter

                func_802539A4(&g_ctx, &g_mem);    // pre-frame setup
                // SKIP func_8003EBD4 (frame gate) — always fails without VRetrace
                func_8024135C(&g_ctx, &g_mem);    // descriptor processing
                func_8003D314(&g_ctx, &g_mem);    // delete queue processing
                func_8003FF00(&g_ctx, &g_mem);    // birth queue → tree insertion
                func_8003D150(&g_ctx, &g_mem);    // process queue
                g_ctx.r[3] = 0x8003E370;          // execute callback
                func_8003D7E0(&g_ctx, &g_mem);    // LAYER EXECUTE DISPATCH

                // Draw phase — calls fn_table[3] for each process
                // 0x8003E390 calls func_8003D51C which dispatches via fn_table[3]
                extern void func_800404CC(PPCContext* ctx, Memory* mem);  // GX draw-done
                g_ctx.r[3] = 0x8003E390;          // draw callback
                func_8003D7E0(&g_ctx, &g_mem);    // LAYER DRAW DISPATCH
                func_800404CC(&g_ctx, &g_mem);     // GX draw-done sync

                g_ctx.r[3] = 1;                   // frame processed
                func_802449AC(&g_ctx, &g_mem);    // frame counter update
            }
            func_80006264(&g_ctx, &g_mem);  // main loop cleanup

            frame++;
            if (frame <= 10 || frame % 60 == 0) {
                int16_t ss = (int16_t)g_mem.read16(g_ctx.r[13] - 30754);
                // Check game info values that change during gameplay
                uint32_t gi = 0x803E4AB4;
                uint32_t gi_counter = g_mem.read32(gi + 2496);
                uint8_t gi_3210 = g_mem.read8(gi + 3210);
                uint8_t gi_3215 = g_mem.read8(gi + 3215);
                uint8_t gi_3216 = g_mem.read8(gi + 3216);
                // Check if any process internal state changed from initial
                // (environment process at 0x804003E0)
                uint32_t env_val = g_mem.read32(0x804003E0 + 0x100);
                // Timing state
                uint32_t time_val = g_mem.read32(0x80405EC0 + 0x100);
                // SDA frame counter at r13(-30792)
                uint32_t frame_ctr = g_mem.read32(g_ctx.r[13] - 30792);
                fprintf(stderr, "[*] Frame %d (state=%d cnt=%u f=%u/%u/%u env=0x%X tm=0x%X fc=0x%X)\n",
                        frame, ss, gi_counter, gi_3210, gi_3215, gi_3216,
                        env_val, time_val, frame_ctr);
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

        // ---- Render scene ----
        if (g_room_model_loaded) {
            using namespace gcrecomp::gx;

            // Diagnostic: dump the matrix in slot 0 BEFORE we clobber it
            // (this is whatever the game's dispatch wrote since our last
            // frame). Snapshot counters so we know if the game actually
            // touched the slot between renders.
            static uint64_t s_last_counter_at_render[64];
            static int s_mtx_log_count = 0;
            if (s_mtx_log_count < 8) {
                uint64_t now = GXGetMatrixWriteCounter(0);
                if (now != s_last_counter_at_render[0] && now != 0) {
                    float pre[3][4];
                    GXGetMatrixSlot(0, pre);
                    fprintf(stderr,
                            "[CAM] f=%d game wrote slot 0 (counter %llu): "
                            "[%.4f %.4f %.4f %.4f]\n"
                            "                                "
                            "[%.4f %.4f %.4f %.4f]\n"
                            "                                "
                            "[%.4f %.4f %.4f %.4f]\n",
                            s_mtx_log_count,
                            (unsigned long long)now,
                            pre[0][0], pre[0][1], pre[0][2], pre[0][3],
                            pre[1][0], pre[1][1], pre[1][2], pre[1][3],
                            pre[2][0], pre[2][1], pre[2][2], pre[2][3]);
                    fflush(stderr);
                } else {
                    fprintf(stderr,
                            "[CAM] f=%d slot 0 unchanged since last render "
                            "(counter=%llu)\n",
                            s_mtx_log_count, (unsigned long long)now);
                }
                s_mtx_log_count++;
            }
            // Snapshot counters at THIS POINT (before our render touches anything).
            for (uint32_t id = 0; id < 64; ++id) {
                s_last_counter_at_render[id] = GXGetMatrixWriteCounter(id);
            }

            // Try using the game's matrix if env var is set (opt-in).
            static const bool use_game_camera =
                std::getenv("WW_USE_GAME_CAMERA") != nullptr;
            bool used_game_mtx = false;
            float game_mv[3][4];
            if (use_game_camera && GXGetMatrixSlot(0, game_mv)) {
                // Sanity check: not all zeros, not identity scaled to zero.
                float sum = 0.0f;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) sum += fabsf(game_mv[r][c]);
                if (sum > 1e-6f) {
                    GXLoadPosMtxImm(game_mv, 0);
                    GXSetCurrentMtx(0);
                    used_game_mtx = true;
                    static int s_logged = 0;
                    if (s_logged++ < 5) {
                        fprintf(stderr,
                                "[CAM] using game matrix slot 0: "
                                "[%.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f / "
                                "%.3f %.3f %.3f %.3f]\n",
                                game_mv[0][0], game_mv[0][1], game_mv[0][2], game_mv[0][3],
                                game_mv[1][0], game_mv[1][1], game_mv[1][2], game_mv[1][3],
                                game_mv[2][0], game_mv[2][1], game_mv[2][2], game_mv[2][3]);
                        fflush(stderr);
                    }
                }
            }

            // Rotate around Y axis, tilt down to see the island from above
            g_camera_angle += 0.006f;

            float sc = 1.0f / 14000.0f;  // scale to fit water + island

            // Y rotation (azimuth spin)
            float cy = cosf(g_camera_angle), sy = sinf(g_camera_angle);
            // X rotation (tilt ~45 degrees)
            float tilt_angle = -0.8f;
            float cx = cosf(tilt_angle), sx = sinf(tilt_angle);

            float mv[3][4] = {
                { cy * sc,              0.0f,        sy * sc,              0.0f },
                { sx * sy * sc,         cx * sc,    -sx * cy * sc,        -0.55f },
                { -cx * sy * sc,        sx * sc,     cx * cy * sc,         0.0f },
            };
            if (!used_game_mtx) {
                GXLoadPosMtxImm(mv, 0);
                GXSetCurrentMtx(0);
            }

            // Orthographic projection with aspect correction and proper Z
            float proj[4][4] = {};
            proj[0][0] = 720.0f / 1280.0f;
            proj[1][1] = 1.0f;
            proj[2][2] = 0.5f;   // proper Z range for depth buffer precision
            proj[2][3] = 0.5f;   // offset: view Z=0 maps to depth 0.5
            proj[3][3] = 1.0f;
            GXSetProjection(proj, 0);
            GXSetViewport(0, 0, 1280, 720, 0.0f, 1.0f);
            GXSetZMode(true, GX_LEQUAL, true);
            GXSetCullMode(GX_CULL_NONE);

            // Render skybox first (behind everything, no depth write)
            if (g_sky_model_loaded) {
                // Skybox uses a much larger scale and no depth
                float sky_sc = 1.0f / 200000.0f;
                float sky_mv[3][4] = {
                    { cy * sky_sc,              0.0f,        sy * sky_sc,              0.0f },
                    { sx * sy * sky_sc,         cx * sky_sc, -sx * cy * sky_sc,         0.0f },
                    { -cx * sy * sky_sc,        sx * sky_sc,  cx * cy * sky_sc,         0.0f },
                };
                GXLoadPosMtxImm(sky_mv, 0);
                GXSetCurrentMtx(0);
                GXSetZMode(false, GX_LEQUAL, false); // no depth for skybox
                render_j3d_model(g_sky_model, g_sky_tex_objs, g_sky_textures_loaded, false);
                // Restore scene matrices
                GXLoadPosMtxImm(mv, 0);
                GXSetCurrentMtx(0);
            }

            // Render island terrain (opaque)
            render_j3d_model(g_room_model, g_tex_objs, g_textures_loaded, false);

            // Render ocean water (translucent)
            if (g_water_model_loaded) {
                render_j3d_model(g_water_model, g_water_tex_objs, g_water_textures_loaded, true);
            }

            // ---- Render spawn markers (one cube per spawned actor) ----
            // Each marker is a small cube in world coordinates at the actor's
            // position. Coordinates use the same 1/14000 scale as the room.
            // Profile-id maps to a color so different actor types are visible.
            if (!g_spawn_markers.empty()) {
                GXSetZMode(true, GX_LEQUAL, true);
                GXSetCullMode(GX_CULL_NONE);
                GXClearVtxDesc();
                GXSetVtxDesc(GX_VA_POS,  GX_DIRECT);
                GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);

                // Cube half-edge in world units. Room model spans ~14000
                // units; pick something visible at 1/14000 scale.
                const float h = 800.0f;
                // 6 cube faces × 6 verts (two triangles per face) = 36 verts.
                static const float cube_verts[6 * 6 * 3] = {
                    // -Z face (front), CCW
                    -1,-1,-1,  +1,-1,-1,  +1,+1,-1,
                    -1,-1,-1,  +1,+1,-1,  -1,+1,-1,
                    // +Z face (back)
                    +1,-1,+1,  -1,-1,+1,  -1,+1,+1,
                    +1,-1,+1,  -1,+1,+1,  +1,+1,+1,
                    // -X face (left)
                    -1,-1,+1,  -1,-1,-1,  -1,+1,-1,
                    -1,-1,+1,  -1,+1,-1,  -1,+1,+1,
                    // +X face (right)
                    +1,-1,-1,  +1,-1,+1,  +1,+1,+1,
                    +1,-1,-1,  +1,+1,+1,  +1,+1,-1,
                    // -Y face (bottom)
                    -1,-1,+1,  +1,-1,+1,  +1,-1,-1,
                    -1,-1,+1,  +1,-1,-1,  -1,-1,-1,
                    // +Y face (top)
                    -1,+1,-1,  +1,+1,-1,  +1,+1,+1,
                    -1,+1,-1,  +1,+1,+1,  -1,+1,+1,
                };

                // Anchor: average actor world position so markers cluster
                // around the rendered island instead of way off-screen.
                // Computed once on first frame from the spawn markers.
                static float anchor[3] = {0, 0, 0};
                static bool anchor_set = false;
                if (!anchor_set) {
                    double sx = 0, sy_ = 0, sz = 0;
                    for (const auto& m : g_spawn_markers) {
                        sx += m.pos[0]; sy_ += m.pos[1]; sz += m.pos[2];
                    }
                    double n = (double)g_spawn_markers.size();
                    anchor[0] = (float)(sx / n);
                    anchor[1] = (float)(sy_ / n);
                    anchor[2] = (float)(sz / n);
                    anchor_set = true;
                    fprintf(stderr, "[MARKER] anchor=(%.0f, %.0f, %.0f) n=%zu\n",
                            anchor[0], anchor[1], anchor[2],
                            g_spawn_markers.size());
                }

                for (const auto& m : g_spawn_markers) {
                    // World position relative to anchor (so cluster of
                    // actors centers near room origin).
                    float wx = m.pos[0] - anchor[0];
                    float wy = m.pos[1] - anchor[1];
                    float wz = m.pos[2] - anchor[2];
                    // Build a model-view matrix that combines the orbital
                    // camera with this actor's relative translation.
                    float wmv[3][4] = {
                        { cy * sc,        0.0f,    sy * sc,
                          ( cy * wx + sy * wz) * sc },
                        { sx * sy * sc,   cx * sc, -sx * cy * sc,
                          ( sx * sy * wx + cx * wy - sx * cy * wz) * sc - 0.55f },
                        { -cx * sy * sc,  sx * sc, cx * cy * sc,
                          (-cx * sy * wx + sx * wy + cx * cy * wz) * sc },
                    };
                    GXLoadPosMtxImm(wmv, 0);
                    GXSetCurrentMtx(0);

                    // Color per profile (low byte = hue band)
                    uint8_t r = (uint8_t)(0x40 + ((m.profname * 53) & 0xBF));
                    uint8_t g = (uint8_t)(0x40 + ((m.profname * 31) & 0xBF));
                    uint8_t b = (uint8_t)(0x40 + ((m.profname * 17) & 0xBF));

                    GXBegin(GX_TRIANGLES, 0, 36);
                    for (int i = 0; i < 36; ++i) {
                        float x = cube_verts[i * 3 + 0] * h;
                        float y = cube_verts[i * 3 + 1] * h;
                        float z = cube_verts[i * 3 + 2] * h;
                        GXPosition3f32(x, y, z);
                        GXColor4u8(r, g, b, 0xFF);
                        GXSubmitVertex();
                    }
                    GXEnd();
                }

                // Restore main scene matrix.
                GXLoadPosMtxImm(mv, 0);
                GXSetCurrentMtx(0);
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
