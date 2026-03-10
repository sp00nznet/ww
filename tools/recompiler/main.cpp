// =============================================================================
// ww_recompiler - Wind Waker Static Recompilation Tool
//
// Usage: ww_recompiler <input.dol> [options]
//   --map <file>       Load symbol map (Dolphin format)
//   --csv <file>       Load symbol map (CSV format)
//   --output <dir>     Output directory for generated C files
//   --info             Just print DOL info and exit
//   --stats            Print CFG statistics
//
// Pipeline:
//   1. Parse DOL file
//   2. Disassemble all code sections (PowerPC 750)
//   3. Build control flow graph (functions, basic blocks)
//   4. Emit C code for each function
// =============================================================================

#include "ww/dol.h"
#include "ww/ppc.h"
#include "ww/cfg.h"
#include "ww/symbol_map.h"
#include "ww/ppc_to_c.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace ww;

static void print_usage(const char* prog) {
    printf("Wind Waker Static Recompiler (GameCube PowerPC -> C)\n");
    printf("====================================================\n\n");
    printf("Usage: %s <input.dol> [options]\n\n", prog);
    printf("Options:\n");
    printf("  --map <file>    Load Dolphin symbol map\n");
    printf("  --csv <file>    Load CSV symbol map (addr,name)\n");
    printf("  --output <dir>  Output directory (default: ./recompiled)\n");
    printf("  --info          Print DOL info and exit\n");
    printf("  --stats         Print CFG statistics and exit\n");
    printf("  --help          Show this help\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string dol_path;
    std::string map_path;
    std::string csv_path;
    std::string output_dir = "recompiled";
    bool info_only = false;
    bool stats_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            map_path = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (strcmp(argv[i], "--info") == 0) {
            info_only = true;
        } else if (strcmp(argv[i], "--stats") == 0) {
            stats_only = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            dol_path = argv[i];
        }
    }

    if (dol_path.empty()) {
        fprintf(stderr, "Error: No DOL file specified\n");
        return 1;
    }

    // ---- Load DOL ----
    printf("\n");
    printf("  ~~ Wind Waker Static Recompiler ~~\n");
    printf("  GameCube PowerPC 750 (Gekko) -> Native C\n");
    printf("  \"The wind... it is blowing.\"\n");
    printf("\n");

    DOLFile dol;
    printf("[*] Loading DOL: %s\n", dol_path.c_str());
    if (!dol.load(dol_path)) {
        fprintf(stderr, "Failed to load DOL file\n");
        return 1;
    }
    dol.print_info();

    if (info_only) return 0;

    // ---- Load symbols ----
    SymbolMap syms;
    if (!map_path.empty()) syms.load_dolphin_map(map_path);
    if (!csv_path.empty()) syms.load_csv(csv_path);

    // ---- Build CFG ----
    printf("\n[*] Building control flow graph...\n");
    CFG cfg;
    cfg.build(dol);
    cfg.print_stats();

    if (stats_only) return 0;

    // ---- Emit C code ----
    printf("\n[*] Generating C code -> %s/\n", output_dir.c_str());

    // Create output directory
    std::string mkdir_cmd = "mkdir -p " + output_dir;
    system(mkdir_cmd.c_str());

    // Emit one .c file per function (for faster compilation)
    // Group into files of ~100 functions each
    int file_index = 0;
    int func_count = 0;
    FILE* current_file = nullptr;

    for (auto& [addr, func] : cfg.functions) {
        if (func_count % 100 == 0) {
            if (current_file) fclose(current_file);
            char filename[256];
            snprintf(filename, sizeof(filename), "%s/recomp_%04d.cpp",
                     output_dir.c_str(), file_index++);
            current_file = fopen(filename, "w");
            if (!current_file) {
                fprintf(stderr, "Failed to create %s\n", filename);
                return 1;
            }
            emit_file_header(current_file);
        }

        // Get function name
        std::string fname = syms.get_name(addr);
        func.name = fname;

        // Forward declare
        fprintf(current_file, "void %s(PPCContext* ctx, Memory* mem);\n", fname.c_str());
        func_count++;
    }

    // Now emit function bodies
    file_index = 0;
    func_count = 0;
    if (current_file) fclose(current_file);
    current_file = nullptr;

    for (auto& [addr, func] : cfg.functions) {
        if (func_count % 100 == 0) {
            if (current_file) fclose(current_file);
            char filename[256];
            snprintf(filename, sizeof(filename), "%s/recomp_%04d.cpp",
                     output_dir.c_str(), file_index++);
            current_file = fopen(filename, "a"); // Append after declarations
        }

        fprintf(current_file, "\n// ---- %s @ 0x%08X ----\n", func.name.c_str(), addr);
        fprintf(current_file, "void %s(PPCContext* ctx, Memory* mem) {\n", func.name.c_str());

        PPCToCEmitter emitter(current_file);

        for (uint32_t block_addr : func.block_addrs) {
            auto& block = func.blocks[block_addr];
            fprintf(current_file, "label_%08X:\n", block_addr);

            for (const auto& insn : block.instructions) {
                emitter.emit_insn(insn);
            }
        }

        fprintf(current_file, "}\n");
        func_count++;
    }

    if (current_file) fclose(current_file);

    printf("\n[*] Done! Generated %d files with %d functions.\n", file_index, func_count);
    printf("[*] The wind is at your back. Set sail!\n\n");

    return 0;
}
