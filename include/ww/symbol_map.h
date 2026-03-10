#pragma once
// =============================================================================
// Symbol Map - Shared definitions
// =============================================================================

#include <cstdint>
#include <string>
#include <map>

namespace ww {

struct Symbol {
    uint32_t    address;
    uint32_t    size;
    std::string name;
    bool        is_function;
};

struct SymbolMap {
    std::map<uint32_t, Symbol> symbols;

    bool load_dolphin_map(const std::string& path);
    bool load_csv(const std::string& path);
    const Symbol* find(uint32_t addr) const;
    std::string get_name(uint32_t addr) const;
};

} // namespace ww
