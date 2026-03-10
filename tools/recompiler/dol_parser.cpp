// =============================================================================
// DOL File Parser - The GameCube executable format
// First thing in the pipeline: crack open the DOL and map it to memory
// =============================================================================

#include "ww/dol.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ww {

bool DOLFile::load(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "[DOL] Failed to open: %s\n", path.c_str());
        return false;
    }
    bool ok = load(fp);
    fclose(fp);
    return ok;
}

bool DOLFile::load(FILE* fp) {
    // Read raw header
    if (fread(&header, sizeof(DOLHeader), 1, fp) != 1) {
        fprintf(stderr, "[DOL] Failed to read header\n");
        return false;
    }

    // Byte-swap all header fields (big-endian -> little-endian)
    for (int i = 0; i < 7; i++) {
        header.text_offsets[i]   = bswap32(header.text_offsets[i]);
        header.text_addresses[i] = bswap32(header.text_addresses[i]);
        header.text_sizes[i]     = bswap32(header.text_sizes[i]);
    }
    for (int i = 0; i < 11; i++) {
        header.data_offsets[i]   = bswap32(header.data_offsets[i]);
        header.data_addresses[i] = bswap32(header.data_addresses[i]);
        header.data_sizes[i]     = bswap32(header.data_sizes[i]);
    }
    header.bss_address  = bswap32(header.bss_address);
    header.bss_size     = bswap32(header.bss_size);
    header.entry_point  = bswap32(header.entry_point);

    entry_point = header.entry_point;
    bss_address = header.bss_address;
    bss_size    = header.bss_size;

    // Load text sections
    for (int i = 0; i < 7; i++) {
        if (header.text_sizes[i] == 0) continue;

        DOLSection sec;
        sec.file_offset = header.text_offsets[i];
        sec.address     = header.text_addresses[i];
        sec.size        = header.text_sizes[i];
        sec.is_text     = true;
        sec.index       = i;
        sec.data.resize(sec.size);

        fseek(fp, sec.file_offset, SEEK_SET);
        if (fread(sec.data.data(), 1, sec.size, fp) != sec.size) {
            fprintf(stderr, "[DOL] Failed to read text section %d\n", i);
            return false;
        }
        sections.push_back(std::move(sec));
    }

    // Load data sections
    for (int i = 0; i < 11; i++) {
        if (header.data_sizes[i] == 0) continue;

        DOLSection sec;
        sec.file_offset = header.data_offsets[i];
        sec.address     = header.data_addresses[i];
        sec.size        = header.data_sizes[i];
        sec.is_text     = false;
        sec.index       = i;
        sec.data.resize(sec.size);

        fseek(fp, sec.file_offset, SEEK_SET);
        if (fread(sec.data.data(), 1, sec.size, fp) != sec.size) {
            fprintf(stderr, "[DOL] Failed to read data section %d\n", i);
            return false;
        }
        sections.push_back(std::move(sec));
    }

    // Build flat memory image
    if (!sections.empty()) {
        memory_base = UINT32_MAX;
        memory_end  = 0;
        for (const auto& sec : sections) {
            memory_base = std::min(memory_base, sec.address);
            memory_end  = std::max(memory_end, sec.address + sec.size);
        }
        // Also include BSS region
        if (bss_size > 0) {
            memory_base = std::min(memory_base, bss_address);
            memory_end  = std::max(memory_end, bss_address + bss_size);
        }

        uint32_t total = memory_end - memory_base;
        memory.resize(total, 0);

        for (const auto& sec : sections) {
            memcpy(memory.data() + (sec.address - memory_base),
                   sec.data.data(), sec.size);
        }
    }

    return true;
}

uint8_t DOLFile::read8(uint32_t addr) const {
    if (addr < memory_base || addr >= memory_end) return 0;
    return memory[addr - memory_base];
}

uint16_t DOLFile::read16(uint32_t addr) const {
    if (addr < memory_base || addr + 1 >= memory_end) return 0;
    uint16_t v;
    memcpy(&v, &memory[addr - memory_base], 2);
    return bswap16(v);
}

uint32_t DOLFile::read32(uint32_t addr) const {
    if (addr < memory_base || addr + 3 >= memory_end) return 0;
    uint32_t v;
    memcpy(&v, &memory[addr - memory_base], 4);
    return bswap32(v);
}

bool DOLFile::is_code(uint32_t addr) const {
    for (const auto& sec : sections) {
        if (sec.is_text && addr >= sec.address && addr < sec.address + sec.size)
            return true;
    }
    return false;
}

void DOLFile::print_info() const {
    printf("=== DOL File Info ===\n");
    printf("Entry point: 0x%08X\n", entry_point);
    printf("BSS: 0x%08X - 0x%08X (%u bytes)\n",
           bss_address, bss_address + bss_size, bss_size);
    printf("\nSections:\n");
    for (const auto& sec : sections) {
        printf("  %s%d: file=0x%06X  addr=0x%08X  size=0x%06X (%7u bytes)\n",
               sec.is_text ? "text" : "data",
               sec.index,
               sec.file_offset,
               sec.address,
               sec.size,
               sec.size);
    }
    printf("\nMemory image: 0x%08X - 0x%08X (%u bytes total)\n",
           memory_base, memory_end, memory_end - memory_base);
}

} // namespace ww
