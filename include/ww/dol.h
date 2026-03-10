#pragma once
// =============================================================================
// Wind Waker Static Recompilation - DOL File Parser
// GameCube DOL executable format: up to 7 text + 11 data sections
// All values big-endian uint32
// =============================================================================

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ww {

// DOL header: 0x100 bytes
struct DOLHeader {
    uint32_t text_offsets[7];    // File offsets for text (code) sections
    uint32_t data_offsets[11];   // File offsets for data sections
    uint32_t text_addresses[7]; // Load addresses for text sections
    uint32_t data_addresses[11];// Load addresses for data sections
    uint32_t text_sizes[7];     // Sizes of text sections
    uint32_t data_sizes[11];    // Sizes of data sections
    uint32_t bss_address;       // BSS (uninitialized data) address
    uint32_t bss_size;          // BSS size
    uint32_t entry_point;       // Entry point address
    uint32_t padding[7];        // Unused padding
};
static_assert(sizeof(DOLHeader) == 0x100, "DOL header must be 256 bytes");

struct DOLSection {
    uint32_t file_offset;
    uint32_t address;       // Virtual address in GameCube memory
    uint32_t size;
    bool     is_text;       // true = code, false = data
    int      index;         // Section index (0-6 for text, 0-10 for data)
    std::vector<uint8_t> data;
};

struct DOLFile {
    DOLHeader header;
    std::vector<DOLSection> sections;
    uint32_t entry_point;
    uint32_t bss_address;
    uint32_t bss_size;

    // Flat memory image for address lookups
    std::vector<uint8_t> memory;       // Mapped at base_address
    uint32_t             memory_base;  // Lowest section address
    uint32_t             memory_end;   // Highest address + size

    bool load(const std::string& path);
    bool load(FILE* fp);

    // Read from virtual address space
    uint8_t  read8(uint32_t addr) const;
    uint16_t read16(uint32_t addr) const;
    uint32_t read32(uint32_t addr) const;

    // Check if address is in a code section
    bool is_code(uint32_t addr) const;

    void print_info() const;
};

// Byte-swap helpers (GameCube is big-endian, x86 is little-endian)
inline uint16_t bswap16(uint16_t v) {
    return (v >> 8) | (v << 8);
}

inline uint32_t bswap32(uint32_t v) {
    return ((v >> 24) & 0xFF) |
           ((v >> 8)  & 0xFF00) |
           ((v << 8)  & 0xFF0000) |
           ((v << 24) & 0xFF000000);
}

} // namespace ww
