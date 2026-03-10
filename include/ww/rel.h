#pragma once
// =============================================================================
// Wind Waker Static Recompilation - REL (Relocatable Module) Parser
// REL files are dynamically loaded code modules (like DLLs for GameCube)
// Wind Waker uses RELs extensively for actors, scenes, etc.
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace ww {

struct RELHeader {
    uint32_t module_id;         // Unique module identifier
    uint32_t next;              // Runtime: next module pointer
    uint32_t prev;              // Runtime: prev module pointer
    uint32_t num_sections;      // Number of sections
    uint32_t section_offset;    // Offset to section table
    uint32_t name_offset;       // Offset to module name (often 0)
    uint32_t name_size;         // Size of module name
    uint32_t version;           // REL version (1, 2, or 3)
    uint32_t bss_size;          // Size of BSS section
    uint32_t rel_offset;        // Offset to relocation table
    uint32_t imp_offset;        // Offset to import table
    uint32_t imp_size;          // Size of import table
    uint8_t  prolog_section;    // Section index for _prolog
    uint8_t  epilog_section;    // Section index for _epilog
    uint8_t  unresolved_section;// Section index for _unresolved
    uint8_t  bss_section;       // Section index for BSS (v2+)
    uint32_t prolog_offset;     // Offset of _prolog in its section
    uint32_t epilog_offset;     // Offset of _epilog
    uint32_t unresolved_offset; // Offset of _unresolved
    // v2+ fields:
    uint32_t align;             // Section alignment
    uint32_t bss_align;         // BSS alignment
    // v3+ fields:
    uint32_t fix_size;          // Size of fixed-position data
};

struct RELSection {
    uint32_t offset;            // File offset (bit 0 = executable flag)
    uint32_t size;
    bool     executable;
    std::vector<uint8_t> data;
};

// Relocation types (PowerPC ELF standard)
enum class RELRelocType : uint8_t {
    R_PPC_NONE       = 0,
    R_PPC_ADDR32     = 1,
    R_PPC_ADDR24     = 2,   // Used for branch instructions
    R_PPC_ADDR16     = 3,
    R_PPC_ADDR16_LO  = 4,
    R_PPC_ADDR16_HI  = 5,
    R_PPC_ADDR16_HA  = 6,
    R_PPC_ADDR14     = 7,
    R_PPC_REL24      = 10,  // Relative branch
    R_PPC_REL14      = 11,
    R_DOLPHIN_NOP    = 201, // Skip bytes (Dolphin extension)
    R_DOLPHIN_SECTION= 202, // Set current section
    R_DOLPHIN_END    = 203, // End of relocations
};

struct RELRelocation {
    uint16_t     offset;     // Offset from previous relocation
    RELRelocType type;
    uint8_t      section;    // Target section index
    uint32_t     addend;     // Symbol offset / addend
};

struct RELImport {
    uint32_t module_id;      // 0 = DOL, otherwise REL module ID
    uint32_t offset;         // Offset to relocations for this module
};

struct RELFile {
    RELHeader header;
    std::vector<RELSection>    sections;
    std::vector<RELImport>     imports;
    std::vector<RELRelocation> relocations;
    std::string                name;

    bool load(const std::string& path);

    void print_info() const;
};

} // namespace ww
