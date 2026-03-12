#pragma once
// =============================================================================
// PPC to C Emitter - Shared definitions
// =============================================================================

#include "ww/ppc.h"
#include <cstdio>
#include <vector>

namespace ww {

class PPCToCEmitter {
public:
    FILE* out;
    int   indent_level;
    std::vector<uint32_t> block_addrs; // All block start addresses in current function

    PPCToCEmitter(FILE* f) : out(f), indent_level(1) {}

    void emit(const char* fmt, ...);
    void emit_raw(const char* fmt, ...);
    void emit_insn(const PPCInsn& insn);
};

void emit_file_header(FILE* out);

} // namespace ww
