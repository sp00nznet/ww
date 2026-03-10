// =============================================================================
// REL File Parser - GameCube Relocatable Modules
// Wind Waker loads actors, scenes, and other game logic as REL modules
// Think of them as GameCube DLLs
// =============================================================================

#include "ww/rel.h"
#include "ww/dol.h" // for bswap
#include <cstdio>
#include <cstring>

namespace ww {

bool RELFile::load(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) {
        fprintf(stderr, "[REL] Failed to open: %s\n", path.c_str());
        return false;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Read header
    if (fread(&header, sizeof(RELHeader), 1, fp) != 1) {
        fprintf(stderr, "[REL] Failed to read header\n");
        fclose(fp);
        return false;
    }

    // Byte-swap header
    header.module_id      = bswap32(header.module_id);
    header.num_sections   = bswap32(header.num_sections);
    header.section_offset = bswap32(header.section_offset);
    header.name_offset    = bswap32(header.name_offset);
    header.name_size      = bswap32(header.name_size);
    header.version        = bswap32(header.version);
    header.bss_size       = bswap32(header.bss_size);
    header.rel_offset     = bswap32(header.rel_offset);
    header.imp_offset     = bswap32(header.imp_offset);
    header.imp_size       = bswap32(header.imp_size);
    header.prolog_offset  = bswap32(header.prolog_offset);
    header.epilog_offset  = bswap32(header.epilog_offset);
    header.unresolved_offset = bswap32(header.unresolved_offset);

    if (header.version >= 2) {
        header.align     = bswap32(header.align);
        header.bss_align = bswap32(header.bss_align);
    }
    if (header.version >= 3) {
        header.fix_size  = bswap32(header.fix_size);
    }

    // Read module name
    if (header.name_offset > 0 && header.name_size > 0) {
        name.resize(header.name_size);
        fseek(fp, header.name_offset, SEEK_SET);
        fread(name.data(), 1, header.name_size, fp);
    }

    // Read sections
    fseek(fp, header.section_offset, SEEK_SET);
    sections.resize(header.num_sections);
    for (uint32_t i = 0; i < header.num_sections; i++) {
        uint32_t raw_offset, raw_size;
        fread(&raw_offset, 4, 1, fp);
        fread(&raw_size, 4, 1, fp);
        raw_offset = bswap32(raw_offset);
        raw_size   = bswap32(raw_size);

        sections[i].executable = (raw_offset & 1) != 0;
        sections[i].offset     = raw_offset & ~1u;
        sections[i].size       = raw_size;

        // Read section data
        if (sections[i].offset > 0 && sections[i].size > 0) {
            long saved = ftell(fp);
            sections[i].data.resize(sections[i].size);
            fseek(fp, sections[i].offset, SEEK_SET);
            fread(sections[i].data.data(), 1, sections[i].size, fp);
            fseek(fp, saved, SEEK_SET);
        }
    }

    // Read import table
    if (header.imp_offset > 0 && header.imp_size > 0) {
        fseek(fp, header.imp_offset, SEEK_SET);
        uint32_t num_imports = header.imp_size / 8;
        imports.resize(num_imports);
        for (uint32_t i = 0; i < num_imports; i++) {
            fread(&imports[i].module_id, 4, 1, fp);
            fread(&imports[i].offset, 4, 1, fp);
            imports[i].module_id = bswap32(imports[i].module_id);
            imports[i].offset    = bswap32(imports[i].offset);
        }
    }

    // Read relocations (from each import's relocation list)
    for (const auto& imp : imports) {
        fseek(fp, imp.offset, SEEK_SET);
        while (true) {
            RELRelocation rel;
            uint16_t raw_offset;
            uint8_t  raw_type;
            uint8_t  raw_section;
            uint32_t raw_addend;

            if (fread(&raw_offset, 2, 1, fp) != 1) break;
            if (fread(&raw_type, 1, 1, fp) != 1) break;
            if (fread(&raw_section, 1, 1, fp) != 1) break;
            if (fread(&raw_addend, 4, 1, fp) != 1) break;

            rel.offset  = bswap16(raw_offset);
            rel.type    = static_cast<RELRelocType>(raw_type);
            rel.section = raw_section;
            rel.addend  = bswap32(raw_addend);

            if (rel.type == RELRelocType::R_DOLPHIN_END) break;

            relocations.push_back(rel);
        }
    }

    fclose(fp);
    return true;
}

void RELFile::print_info() const {
    printf("=== REL File Info ===\n");
    printf("Module ID: %u\n", header.module_id);
    printf("Version: %u\n", header.version);
    if (!name.empty()) printf("Name: %s\n", name.c_str());
    printf("BSS size: 0x%X (%u bytes)\n", header.bss_size, header.bss_size);
    printf("Prolog: section %u + 0x%X\n", header.prolog_section, header.prolog_offset);
    printf("Epilog: section %u + 0x%X\n", header.epilog_section, header.epilog_offset);

    printf("\nSections (%u):\n", header.num_sections);
    for (uint32_t i = 0; i < sections.size(); i++) {
        if (sections[i].size == 0) continue;
        printf("  [%2u] offset=0x%06X  size=0x%06X  %s\n",
               i, sections[i].offset, sections[i].size,
               sections[i].executable ? "CODE" : "DATA");
    }

    printf("\nImports (%zu):\n", imports.size());
    for (const auto& imp : imports) {
        printf("  module %u -> relocs at 0x%06X\n", imp.module_id, imp.offset);
    }

    printf("\nRelocations: %zu total\n", relocations.size());
}

} // namespace ww
