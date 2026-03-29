#pragma once
// =============================================================================
// Wind Waker - JKR Archive Mount System
//
// Creates proper JKRMemArchive objects in emulated memory backed by RARC
// archives that have been Yaz0-decompressed from the game disc. Archives
// are linked into the game's sVolumeList so the original recompiled code
// can find resources via getGlbResource / findVolume / getResource.
//
// Reference: zeldaret/tww JKRMemArchive (0x70 bytes), JSUList/JSULink
// =============================================================================

#include "ww/runtime.h"
#include "gcrecomp/rarc.h"
#include <string>
#include <vector>

namespace ww {
namespace jkr {

// ---- Constants ----
static constexpr uint32_t VOLUME_LIST_ADDR = 0x803ED77C;  // sVolumeList (head, tail, count)
static constexpr uint32_t VTABLE_ADDR      = 0x817FF000;  // Shared JKRMemArchive vtable
static constexpr uint32_t VTABLE_FUNC_BASE = 0x817FF100;  // GC addresses for vtable handlers
static constexpr int      VTABLE_ENTRIES   = 20;
static constexpr uint32_t JKR_OBJ_SIZE     = 0x70;        // JKRMemArchive object size

// JKRMemArchive field offsets (from zeldaret/tww decomp)
namespace off {
    static constexpr uint32_t VTABLE         = 0x00;
    static constexpr uint32_t DISPOSER_HEAP  = 0x04;
    static constexpr uint32_t LOADER_LINK    = 0x18;  // JSULink<JKRFileLoader> (16 bytes)
    static constexpr uint32_t VOLUME_NAME    = 0x28;
    static constexpr uint32_t VOLUME_TYPE    = 0x2C;  // 'RARC' = 0x52415243
    static constexpr uint32_t IS_MOUNTED     = 0x30;
    static constexpr uint32_t MOUNT_COUNT    = 0x34;
    static constexpr uint32_t HEAP           = 0x38;
    static constexpr uint32_t MOUNT_MODE     = 0x3C;  // 1 = MEM
    static constexpr uint32_t ENTRY_NUM      = 0x40;
    static constexpr uint32_t ARC_INFO_BLOCK = 0x44;
    static constexpr uint32_t NODES          = 0x48;
    static constexpr uint32_t FILES          = 0x4C;
    static constexpr uint32_t EXPANDED_SIZE  = 0x50;
    static constexpr uint32_t STRING_TABLE   = 0x54;
    static constexpr uint32_t FIELD_0x58     = 0x58;
    static constexpr uint32_t COMPRESSION    = 0x5C;
    static constexpr uint32_t MOUNT_DIR      = 0x60;
    static constexpr uint32_t ARC_HEADER     = 0x64;
    static constexpr uint32_t ARCHIVE_DATA   = 0x68;
    static constexpr uint32_t IS_OPEN        = 0x6C;
}

// JSULink field offsets within the 16-byte link structure
namespace link {
    static constexpr uint32_t OBJECT = 0x00;
    static constexpr uint32_t LIST   = 0x04;
    static constexpr uint32_t PREV   = 0x08;
    static constexpr uint32_t NEXT   = 0x0C;
}

// Host-side mount tracking
struct MountedArchive {
    std::string volume_name;
    uint32_t gc_data_addr;       // GC address of decompressed RARC data
    uint32_t gc_data_size;
    uint32_t gc_jkr_obj_addr;    // GC address of JKRMemArchive object
    gcrecomp::RARCArchive parsed; // host-side parsed file index
};

// Initialize the JKR vtable and register vtable handlers in the func_table.
// Must be called once before any archives are mounted.
void init(FuncTable& ft, Memory& mem);

// Mount a decompressed RARC archive that is already in emulated memory.
// Allocates a JKRMemArchive object, parses the RARC, links into sVolumeList.
// Returns the GC address of the JKRMemArchive object, or 0 on failure.
uint32_t mount(const char* name, uint32_t gc_data_addr, uint32_t gc_data_size,
               Memory& mem);

// Look up a file across all mounted archives by filename or path.
// Returns the GC address of the file data within the archive, or 0.
uint32_t get_resource(const char* name, Memory& mem);

// Find a mounted archive by GC object address.
// Returns nullptr if not found.
const MountedArchive* find_by_obj(uint32_t gc_jkr_obj_addr);

// Register getGlbResource and findVolume replacements in the func_table.
void register_os_funcs(FuncTable& ft, Memory& mem);

} // namespace jkr
} // namespace ww
