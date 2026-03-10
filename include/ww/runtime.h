#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Runtime
//
// Game-specific runtime layer built on top of gcrecomp's generic runtime.
// Re-exports PPCContext, Memory, FuncTable from gcrecomp, and adds
// Wind Waker-specific initialization and OS HLE.
// =============================================================================

#include "gcrecomp/runtime.h"

namespace ww {

// Re-export core types from gcrecomp
using PPCContext    = gcrecomp::PPCContext;
using Memory        = gcrecomp::Memory;
using FuncTable     = gcrecomp::FuncTable;
using RecompiledFunc = gcrecomp::RecompiledFunc;

// ---- Global runtime state (Wind Waker instance) ---------------------------
extern PPCContext g_ctx;
extern Memory     g_mem;
extern FuncTable  g_func_table;

bool runtime_init();
void runtime_shutdown();

// ---- Wind Waker OS HLE ---------------------------------------------------
void init_low_memory(Memory* mem);
void register_os_functions();
RecompiledFunc lookup_os_func(const char* name);
void set_game_root(const std::string& path);

} // namespace ww
