// =============================================================================
// ww_dol_info - Quick DOL file inspector
// Dumps section layout, entry point, and basic stats
// Usage: ww_dol_info <file.dol>
// =============================================================================

#include "ww/dol.h"
#include "ww/ppc.h"
#include <cstdio>

using namespace ww;

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <file.dol>\n", argv[0]);
        printf("Inspect a GameCube DOL executable file.\n");
        return 1;
    }

    DOLFile dol;
    if (!dol.load(argv[1])) {
        fprintf(stderr, "Failed to load: %s\n", argv[1]);
        return 1;
    }

    dol.print_info();

    // Count instructions in code sections
    uint32_t total_insns = 0;
    uint32_t unknown_insns = 0;
    for (const auto& sec : dol.sections) {
        if (!sec.is_text) continue;
        auto insns = ppc_disasm_range(sec.data.data(), sec.address, sec.size);
        total_insns += (uint32_t)insns.size();
        for (const auto& insn : insns) {
            if (insn.type == PPCInsnType::UNKNOWN) unknown_insns++;
        }
    }

    printf("\nInstruction stats:\n");
    printf("  Total instructions: %u\n", total_insns);
    printf("  Recognized:         %u (%.1f%%)\n",
           total_insns - unknown_insns,
           100.0f * (total_insns - unknown_insns) / total_insns);
    printf("  Unknown:            %u (%.1f%%)\n",
           unknown_insns,
           100.0f * unknown_insns / total_insns);

    // First 10 instructions from entry point
    printf("\nFirst instructions at entry point 0x%08X:\n", dol.entry_point);
    for (int i = 0; i < 10; i++) {
        uint32_t addr = dol.entry_point + i * 4;
        uint32_t raw = dol.read32(addr);
        PPCInsn insn = ppc_disasm(raw, addr);
        printf("  0x%08X: %08X  %s\n", addr, raw, insn.mnemonic.c_str());
    }

    return 0;
}
