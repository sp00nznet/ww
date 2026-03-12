#pragma once
// =============================================================================
// Control Flow Graph - Shared definitions
// =============================================================================

#include "ww/ppc.h"
#include "ww/dol.h"
#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <map>

namespace ww {

struct BasicBlock {
    uint32_t start;
    uint32_t end;
    std::vector<uint32_t> successors;
    std::vector<uint32_t> predecessors;
    std::vector<PPCInsn>  instructions;
    bool     is_entry;
    bool     is_return;
};

struct Function {
    uint32_t entry;
    std::string name;
    std::vector<uint32_t> block_addrs;
    std::map<uint32_t, BasicBlock> blocks;
    std::set<uint32_t> calls;
    bool     is_leaf;
};

struct CFG {
    std::map<uint32_t, Function> functions;
    std::set<uint32_t> call_targets;

    void build(const DOLFile& dol);
    void scan_targets(const DOLFile& dol);     // Phase 1: populate call_targets
    void add_extra_entries(const std::vector<uint32_t>& addrs);
    void build_functions(const DOLFile& dol);   // Phase 2: build from call_targets
    void discover_functions(const DOLFile& dol);
    void build_blocks(Function& func, const DOLFile& dol);
    void print_stats() const;
};

} // namespace ww
