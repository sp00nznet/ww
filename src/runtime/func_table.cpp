// =============================================================================
// Wind Waker - Function Table and Runtime Init/Shutdown
//
// The FuncTable member functions are provided by gcrecomp_runtime.
// This file defines the global instance and Wind Waker-specific
// initialization that wires up OS HLE and low-memory state.
// =============================================================================

#include "ww/runtime.h"
#include <cstdio>

namespace ww {

FuncTable g_func_table;

bool runtime_init() {
    printf("[Runtime] Initializing Wind Waker runtime...\n");

    // Initialize memory (provided by gcrecomp).
    // 64 MB lets us host the standard 24 MB main RAM (0x80000000-0x81800000)
    // plus relocated REL data sections, which the recompiler places starting
    // at 0x82000000+. Without the extra room the prologs blow up on the very
    // first lwz of their static profile descriptor.
    if (!g_mem.init(64 * 1024 * 1024)) return false;

    // Initialize CPU context
    g_ctx.reset();

    // Set initial stack pointer (top of main RAM - some space for OS)
    g_ctx.r[1] = Memory::MAIN_RAM_BASE + g_mem.ram_size - 0x100;

    // Initialize Dolphin OS low-memory state (clock speeds, arena, game ID, etc.)
    init_low_memory(&g_mem);

    // Register OS function replacements (Wind Waker-specific HLE)
    register_os_functions();

    printf("[Runtime] Ready. Stack at 0x%08X\n", g_ctx.r[1]);
    return true;
}

void runtime_shutdown() {
    g_mem.shutdown();
    printf("[Runtime] Shutdown complete.\n");
}

} // namespace ww
