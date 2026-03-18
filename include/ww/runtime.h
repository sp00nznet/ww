#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Runtime
//
// Game-specific runtime layer built on top of gcrecomp's generic runtime.
// Re-exports PPCContext, Memory, FuncTable from gcrecomp, and adds
// Wind Waker-specific initialization and OS HLE.
// =============================================================================

#include "gcrecomp/runtime.h"
#include "gcrecomp/yaz0.h"
#include "gcrecomp/rarc.h"

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

// ---- Disc image support (delegates to gcrecomp) --------------------------
inline bool mount_disc_image(const char* p, Memory* m) { return gcrecomp::mount_disc_image(p, m); }
inline size_t disc_read(uint32_t off, void* dst, size_t len) { return gcrecomp::disc_read(off, dst, len); }
inline bool is_disc_mounted() { return gcrecomp::is_disc_mounted(); }

} // namespace ww
