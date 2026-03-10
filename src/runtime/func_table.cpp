// =============================================================================
// Function Table - Maps GameCube addresses to native recompiled functions
// Critical for indirect calls: virtual methods, function pointers, switch tables
// =============================================================================

#include "ww/runtime.h"
#include <cstdio>

namespace ww {

FuncTable g_func_table;

void FuncTable::register_func(uint32_t gc_addr, RecompiledFunc func) {
    table[gc_addr] = func;
}

RecompiledFunc FuncTable::lookup(uint32_t gc_addr) const {
    auto it = table.find(gc_addr);
    if (it != table.end()) return it->second;
    return nullptr;
}

void FuncTable::call(uint32_t gc_addr, PPCContext* ctx, Memory* mem) const {
    RecompiledFunc func = lookup(gc_addr);
    if (func) {
        func(ctx, mem);
    } else {
        fprintf(stderr, "[FuncTable] Unresolved indirect call to 0x%08X\n", gc_addr);
        fprintf(stderr, "  LR=0x%08X  r3=0x%08X  r4=0x%08X\n", ctx->lr, ctx->r[3], ctx->r[4]);
    }
}

bool runtime_init() {
    printf("[Runtime] Initializing...\n");

    // Initialize memory
    if (!g_mem.init()) return false;

    // Initialize CPU context
    g_ctx.reset();

    // Set initial stack pointer (top of main RAM - some space for OS)
    g_ctx.r[1] = Memory::MAIN_RAM_BASE + Memory::MAIN_RAM_SIZE - 0x100;
    // r2 = small data area (SDA) base - will be set by DOL loader
    // r13 = small data area 2 (SDA2) base - will be set by DOL loader

    // Initialize Dolphin OS low-memory state (clock speeds, arena, game ID, etc.)
    init_low_memory(&g_mem);

    // Register OS function replacements
    register_os_functions();

    printf("[Runtime] Ready. Stack at 0x%08X\n", g_ctx.r[1]);
    return true;
}

void runtime_shutdown() {
    g_mem.shutdown();
    printf("[Runtime] Shutdown complete.\n");
}

} // namespace ww
