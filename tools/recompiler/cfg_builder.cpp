// =============================================================================
// Control Flow Graph Builder
// Analyzes disassembled PPC code to identify functions, basic blocks,
// and call targets. This is how we turn a flat binary into structured C.
// =============================================================================

#include "ww/cfg.h"
#include <cstdio>
#include <queue>
#include <algorithm>

namespace ww {

void CFG::build(const DOLFile& dol) {
    printf("[CFG] Building control flow graph...\n");

    // Start with the entry point
    call_targets.insert(dol.entry_point);

    // Phase 1: Linear scan for bl (branch-and-link) targets
    // This catches most direct function calls
    for (const auto& sec : dol.sections) {
        if (!sec.is_text) continue;

        auto insns = ppc_disasm_range(sec.data.data(), sec.address, sec.size);
        for (const auto& insn : insns) {
            if (insn.type == PPCInsnType::B && insn.link) {
                // bl target -> definitely a function
                uint32_t target = insn.branch_target;
                if (dol.is_code(target)) {
                    call_targets.insert(target);
                }
            }
        }
    }

    printf("[CFG] Phase 1: Found %zu call targets from bl scan\n", call_targets.size());

    // Phase 2: Build functions and basic blocks
    discover_functions(dol);

    printf("[CFG] Phase 2: Built %zu functions\n", functions.size());
}

void CFG::discover_functions(const DOLFile& dol) {
    for (uint32_t entry : call_targets) {
        if (!dol.is_code(entry)) continue;

        Function func;
        func.entry = entry;
        func.is_leaf = true;

        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "func_%08X", entry);
        func.name = name_buf;

        build_blocks(func, dol);
        functions[entry] = std::move(func);
    }
}

void CFG::build_blocks(Function& func, const DOLFile& dol) {
    std::set<uint32_t> block_starts;
    std::queue<uint32_t> work;

    block_starts.insert(func.entry);
    work.push(func.entry);

    // First pass: discover block boundaries
    while (!work.empty()) {
        uint32_t addr = work.front();
        work.pop();

        uint32_t pc = addr;
        while (dol.is_code(pc)) {
            uint32_t raw = dol.read32(pc);
            PPCInsn insn = ppc_disasm(raw, pc);

            if (insn.is_branch()) {
                if (insn.type == PPCInsnType::B && !insn.link) {
                    // Unconditional branch (not a call)
                    uint32_t target = insn.branch_target;
                    if (dol.is_code(target) && !block_starts.count(target)) {
                        block_starts.insert(target);
                        work.push(target);
                    }
                    break;
                }
                else if (insn.type == PPCInsnType::BC) {
                    // Conditional branch
                    uint32_t target = insn.branch_target;
                    uint32_t fall = pc + 4;
                    if (dol.is_code(target) && !block_starts.count(target)) {
                        block_starts.insert(target);
                        work.push(target);
                    }
                    if (!block_starts.count(fall)) {
                        block_starts.insert(fall);
                        work.push(fall);
                    }
                    break;
                }
                else if (insn.type == PPCInsnType::B && insn.link) {
                    // Function call — note target but continue this block
                    func.calls.insert(insn.branch_target);
                    func.is_leaf = false;
                }
                else if (insn.is_return()) {
                    break;
                }
                else if (insn.type == PPCInsnType::BCCTR) {
                    // Indirect branch via CTR — could be switch table
                    break;
                }
            }

            pc += 4;
        }
    }

    // Second pass: build actual basic blocks
    std::vector<uint32_t> sorted_starts(block_starts.begin(), block_starts.end());
    std::sort(sorted_starts.begin(), sorted_starts.end());
    func.block_addrs = sorted_starts;

    for (size_t i = 0; i < sorted_starts.size(); i++) {
        uint32_t start = sorted_starts[i];
        uint32_t limit = (i + 1 < sorted_starts.size()) ? sorted_starts[i + 1] : start + 0x10000;

        BasicBlock block;
        block.start = start;
        block.is_entry = (start == func.entry);
        block.is_return = false;

        uint32_t pc = start;
        while (pc < limit && dol.is_code(pc)) {
            uint32_t raw = dol.read32(pc);
            PPCInsn insn = ppc_disasm(raw, pc);
            block.instructions.push_back(insn);

            if (insn.is_branch()) {
                if (insn.is_return()) {
                    block.is_return = true;
                } else if (insn.type == PPCInsnType::B && !insn.link) {
                    block.successors.push_back(insn.branch_target);
                } else if (insn.type == PPCInsnType::BC) {
                    block.successors.push_back(insn.branch_target);
                    block.successors.push_back(pc + 4);
                }
                pc += 4;
                break;
            }
            pc += 4;
        }

        block.end = pc;
        func.blocks[start] = std::move(block);
    }

    // Wire up predecessors
    for (auto& [addr, block] : func.blocks) {
        for (uint32_t succ : block.successors) {
            if (func.blocks.count(succ)) {
                func.blocks[succ].predecessors.push_back(addr);
            }
        }
    }
}

void CFG::print_stats() const {
    uint32_t total_blocks = 0;
    uint32_t total_insns = 0;
    uint32_t leaf_funcs = 0;

    for (const auto& [addr, func] : functions) {
        total_blocks += (uint32_t)func.blocks.size();
        for (const auto& [_, block] : func.blocks) {
            total_insns += (uint32_t)block.instructions.size();
        }
        if (func.is_leaf) leaf_funcs++;
    }

    printf("=== CFG Statistics ===\n");
    printf("Functions:    %zu\n", functions.size());
    printf("Leaf funcs:   %u\n", leaf_funcs);
    printf("Basic blocks: %u\n", total_blocks);
    printf("Instructions: %u\n", total_insns);
}

} // namespace ww
