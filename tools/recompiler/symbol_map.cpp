// =============================================================================
// Symbol Map - Maps addresses to function/variable names
// Can load from:
//   - Dolphin .map files
//   - decomp-toolkit symbol files
//   - Custom CSV format
// =============================================================================

#include "ww/symbol_map.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ww {

bool SymbolMap::load_dolphin_map(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return false;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '.') continue;

        uint32_t addr1, size, addr2, flags;
        char name[256];
        if (sscanf(line, "%08x %08x %08x %x %255s", &addr1, &size, &addr2, &flags, name) == 5) {
            Symbol sym;
            sym.address = addr1;
            sym.size = size;
            sym.name = name;
            sym.is_function = (size > 0);
            symbols[addr1] = sym;
        }
    }

    fclose(fp);
    printf("[SymbolMap] Loaded %zu symbols from %s\n", symbols.size(), path.c_str());
    return true;
}

bool SymbolMap::load_csv(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return false;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        uint32_t addr;
        char name[256];
        if (sscanf(line, "0x%x,%255s", &addr, name) == 2 ||
            sscanf(line, "%x,%255s", &addr, name) == 2) {
            Symbol sym;
            sym.address = addr;
            sym.size = 0;
            sym.name = name;
            sym.is_function = true;
            symbols[addr] = sym;
        }
    }

    fclose(fp);
    printf("[SymbolMap] Loaded %zu symbols from %s\n", symbols.size(), path.c_str());
    return true;
}

const Symbol* SymbolMap::find(uint32_t addr) const {
    auto it = symbols.find(addr);
    return (it != symbols.end()) ? &it->second : nullptr;
}

std::string SymbolMap::get_name(uint32_t addr) const {
    const Symbol* sym = find(addr);
    if (sym) return sym->name;
    char buf[32];
    snprintf(buf, sizeof(buf), "func_%08X", addr);
    return buf;
}

} // namespace ww
