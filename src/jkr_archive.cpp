// =============================================================================
// JKR Archive Mount System
//
// Implements a native JKR (J Kernel Resource) archive mounting system that
// creates proper JKRMemArchive objects in emulated memory. These objects have
// working vtable methods and are linked into the game's sVolumeList, allowing
// the original recompiled code to find resources via getGlbResource.
//
// Reference: zeldaret/tww JKRMemArchive, JKRFileLoader, JKRArchive
// =============================================================================

#include "ww/jkr_archive.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace ww {
namespace jkr {

// ---- Global State ----

static std::vector<MountedArchive> g_archives;
static bool g_vtable_initialized = false;

// Bump allocator for JKR objects (uses top of arena, grows downward to avoid
// colliding with the game's upward-growing bump allocator)
static uint32_t g_jkr_alloc_ptr = 0x817FE000;  // Below vtable area

static uint32_t jkr_alloc(uint32_t size, uint32_t align = 32) {
    g_jkr_alloc_ptr = (g_jkr_alloc_ptr - size) & ~(align - 1);
    return g_jkr_alloc_ptr;
}

// ---- Helper: read null-terminated string from emulated memory ----

static std::string read_gc_string(Memory& mem, uint32_t addr) {
    std::string s;
    for (int i = 0; i < 256; i++) {
        char c = (char)mem.read8(addr + i);
        if (c == 0) break;
        s += c;
    }
    return s;
}

// ---- Helper: RARC header parsing for GC addresses ----
// Returns GC addresses of internal RARC structures by reading the big-endian
// header in emulated memory. This parallels gcrecomp's rarc_parse but produces
// GC addresses instead of host pointers.

struct RARCLayout {
    uint32_t info_block;      // GC addr of SArcDataInfo
    uint32_t nodes;           // GC addr of SDIDirEntry array
    uint32_t files;           // GC addr of SDIFileEntry array
    uint32_t string_table;    // GC addr of string table
    uint32_t file_data;       // GC addr of file data region
    uint32_t root_name_addr;  // GC addr of root node name in string table
};

static bool parse_rarc_layout(Memory& mem, uint32_t gc_base, uint32_t size,
                              RARCLayout& out) {
    if (size < 0x40) return false;

    // Verify RARC magic
    uint32_t magic = mem.read32(gc_base);
    if (magic != 0x52415243) return false;  // 'RARC'

    uint32_t data_header_off = mem.read32(gc_base + 0x08);  // typically 0x20
    uint32_t file_data_off   = mem.read32(gc_base + 0x0C);

    // Info block at gc_base + data_header_off
    uint32_t info = gc_base + data_header_off;
    out.info_block = info;

    uint32_t node_count    = mem.read32(info + 0x00);
    uint32_t node_off      = mem.read32(info + 0x04);
    uint32_t file_count    = mem.read32(info + 0x08);
    uint32_t file_off      = mem.read32(info + 0x0C);
    uint32_t str_tbl_size  = mem.read32(info + 0x10);
    uint32_t str_tbl_off   = mem.read32(info + 0x14);

    out.nodes        = info + node_off;
    out.files        = info + file_off;
    out.string_table = info + str_tbl_off;
    out.file_data    = gc_base + data_header_off + file_data_off;

    // Root node name: first node's name_offset (at nodes + 0x04)
    if (node_count > 0) {
        uint32_t root_name_off = mem.read32(out.nodes + 0x04);
        out.root_name_addr = out.string_table + root_name_off;
    } else {
        out.root_name_addr = 0;
    }

    (void)str_tbl_size;
    (void)file_count;
    return true;
}

// ---- VTable Handlers ----
// These are called via CALL_INDIRECT when game code uses vtable dispatch
// on our JKRMemArchive objects. r3 = 'this' (GC address of JKR object).

// vt[3]: unmount — decrement mount count
static void vt_unmount(PPCContext* ctx, Memory* mem) {
    uint32_t obj = ctx->r[3];
    uint32_t count = mem->read32(obj + off::MOUNT_COUNT);
    if (count > 0) count--;
    mem->write32(obj + off::MOUNT_COUNT, count);
    printf("[JKR] unmount: obj=0x%08X count=%u\n", obj, count);
}

// vt[5]: getResource(const char* name) — find file by name, return data ptr
static void vt_getResource_name(PPCContext* ctx, Memory* mem) {
    uint32_t obj = ctx->r[3];
    uint32_t name_addr = ctx->r[4];
    std::string name = read_gc_string(*mem, name_addr);

    const MountedArchive* arc = find_by_obj(obj);
    if (!arc) {
        printf("[JKR] getResource: unknown obj 0x%08X\n", obj);
        ctx->r[3] = 0;
        return;
    }

    // Search parsed RARC by filename and full path
    const gcrecomp::RARCFile* file = arc->parsed.find(name.c_str());
    if (!file) file = arc->parsed.find_path(name.c_str());
    if (!file) {
        printf("[JKR] getResource: '%s' not found in %s\n",
               name.c_str(), arc->volume_name.c_str());
        ctx->r[3] = 0;
        return;
    }

    // Compute GC address of file data
    uint8_t* host_base = mem->translate(arc->gc_data_addr);
    const uint8_t* host_data = arc->parsed.file_data(*file, host_base,
                                                      arc->gc_data_size);
    if (!host_data) {
        ctx->r[3] = 0;
        return;
    }
    uint32_t offset = (uint32_t)(host_data - host_base);
    uint32_t gc_addr = arc->gc_data_addr + offset;

    printf("[JKR] getResource: '%s' -> 0x%08X (%u bytes)\n",
           name.c_str(), gc_addr, file->data_size);
    ctx->r[3] = gc_addr;
}

// vt[6]: getResource(u32 type, const char* name) — same as vt[5], type in r4
static void vt_getResource_type_name(PPCContext* ctx, Memory* mem) {
    // Shift args: r4=type (ignored), r5=name
    ctx->r[4] = ctx->r[5];
    vt_getResource_name(ctx, mem);
}

// Generic no-op vtable handler
static void vt_noop(PPCContext* ctx, Memory* mem) {
    // No-op — covers destructors, removeResource, detachResource, etc.
}

// ---- VTable Initialization ----

void init(FuncTable& ft, Memory& mem) {
    if (g_vtable_initialized) return;

    printf("[JKR] Initializing vtable at 0x%08X...\n", VTABLE_ADDR);

    // Register no-op handler for all 20 vtable entries
    for (int i = 0; i < VTABLE_ENTRIES; i++) {
        uint32_t func_addr = VTABLE_FUNC_BASE + i * 4;
        ft.register_func(func_addr, vt_noop);
        mem.write32(VTABLE_ADDR + i * 4, func_addr);
    }

    // Override critical entries with real implementations
    ft.register_func(VTABLE_FUNC_BASE + 3 * 4, vt_unmount);          // unmount
    ft.register_func(VTABLE_FUNC_BASE + 5 * 4, vt_getResource_name); // getResource(name)
    ft.register_func(VTABLE_FUNC_BASE + 6 * 4, vt_getResource_type_name); // getResource(type,name)

    // Initialize sVolumeList to empty (head=0, tail=0, count=0)
    if (mem.read32(VOLUME_LIST_ADDR) == 0 &&
        mem.read32(VOLUME_LIST_ADDR + 4) == 0) {
        mem.write32(VOLUME_LIST_ADDR + 0, 0);  // head
        mem.write32(VOLUME_LIST_ADDR + 4, 0);  // tail
        mem.write32(VOLUME_LIST_ADDR + 8, 0);  // count
    }

    g_vtable_initialized = true;
    printf("[JKR] VTable ready (%d entries, 3 active handlers)\n", VTABLE_ENTRIES);
}

// ---- Mount Archive ----

uint32_t mount(const char* name, uint32_t gc_data_addr, uint32_t gc_data_size,
               Memory& mem) {
    // Parse RARC layout from emulated memory
    RARCLayout layout;
    if (!parse_rarc_layout(mem, gc_data_addr, gc_data_size, layout)) {
        printf("[JKR] Mount failed: invalid RARC at 0x%08X\n", gc_data_addr);
        return 0;
    }

    // Parse RARC on host side for file lookups
    uint8_t* host_data = mem.translate(gc_data_addr);
    MountedArchive arc;
    if (!gcrecomp::rarc_parse(host_data, gc_data_size, arc.parsed)) {
        printf("[JKR] Mount failed: RARC parse error at 0x%08X\n", gc_data_addr);
        return 0;
    }

    // Use provided name, or extract from RARC root node
    if (name && name[0]) {
        arc.volume_name = name;
    } else if (layout.root_name_addr) {
        arc.volume_name = read_gc_string(mem, layout.root_name_addr);
    } else {
        arc.volume_name = "unnamed";
    }

    arc.gc_data_addr = gc_data_addr;
    arc.gc_data_size = gc_data_size;

    // Allocate JKRMemArchive object (0x70 bytes) from top-down allocator
    uint32_t obj = jkr_alloc(JKR_OBJ_SIZE);
    arc.gc_jkr_obj_addr = obj;
    memset(mem.translate(obj), 0, JKR_OBJ_SIZE);

    // Also allocate space for the volume name string in emulated memory
    uint32_t name_gc;
    if (layout.root_name_addr) {
        name_gc = layout.root_name_addr;  // Point into RARC string table
    } else {
        uint32_t name_len = (uint32_t)arc.volume_name.size() + 1;
        name_gc = jkr_alloc(name_len, 4);
        uint8_t* name_dst = mem.translate(name_gc);
        memcpy(name_dst, arc.volume_name.c_str(), name_len);
    }

    // Fill JKRMemArchive fields
    mem.write32(obj + off::VTABLE,         VTABLE_ADDR);
    mem.write32(obj + off::DISPOSER_HEAP,  0x80400010);  // fake heap
    mem.write32(obj + off::VOLUME_NAME,    name_gc);
    mem.write32(obj + off::VOLUME_TYPE,    0x52415243);   // 'RARC'
    mem.write8 (obj + off::IS_MOUNTED,     1);
    mem.write32(obj + off::MOUNT_COUNT,    1);
    mem.write32(obj + off::HEAP,           0x80400010);   // fake heap
    mem.write8 (obj + off::MOUNT_MODE,     1);            // MEM
    mem.write32(obj + off::ENTRY_NUM,      0);
    mem.write32(obj + off::ARC_INFO_BLOCK, layout.info_block);
    mem.write32(obj + off::NODES,          layout.nodes);
    mem.write32(obj + off::FILES,          layout.files);
    mem.write32(obj + off::EXPANDED_SIZE,  0);
    mem.write32(obj + off::STRING_TABLE,   layout.string_table);
    mem.write32(obj + off::FIELD_0x58,     1);
    mem.write32(obj + off::COMPRESSION,    0);            // uncompressed
    mem.write32(obj + off::MOUNT_DIR,      1);            // forward
    mem.write32(obj + off::ARC_HEADER,     gc_data_addr);
    mem.write32(obj + off::ARCHIVE_DATA,   layout.file_data);
    mem.write8 (obj + off::IS_OPEN,        1);

    // Set up JSULink at obj+0x18 (16 bytes: object, list, prev, next)
    uint32_t node = obj + off::LOADER_LINK;
    mem.write32(node + link::OBJECT, obj);
    mem.write32(node + link::LIST,   VOLUME_LIST_ADDR);

    // Prepend to sVolumeList (head, tail, count at VOLUME_LIST_ADDR)
    uint32_t old_head = mem.read32(VOLUME_LIST_ADDR);
    mem.write32(node + link::PREV, 0);          // new head has no prev
    mem.write32(node + link::NEXT, old_head);   // next = old head
    if (old_head) {
        mem.write32(old_head + link::PREV, node);  // old_head.prev = new
    }
    mem.write32(VOLUME_LIST_ADDR + 0, node);    // list.head = new
    if (mem.read32(VOLUME_LIST_ADDR + 4) == 0) {
        mem.write32(VOLUME_LIST_ADDR + 4, node);  // list.tail = new (first entry)
    }
    uint32_t count = mem.read32(VOLUME_LIST_ADDR + 8);
    mem.write32(VOLUME_LIST_ADDR + 8, count + 1);

    g_archives.push_back(std::move(arc));

    printf("[JKR] Mounted '%s' at 0x%08X (obj=0x%08X, %zu files)\n",
           g_archives.back().volume_name.c_str(), gc_data_addr, obj,
           g_archives.back().parsed.files.size());

    return obj;
}

// ---- Resource Lookup ----

uint32_t get_resource(const char* name, Memory& mem) {
    for (auto& arc : g_archives) {
        const gcrecomp::RARCFile* file = arc.parsed.find(name);
        if (!file) file = arc.parsed.find_path(name);
        if (file) {
            uint8_t* host_base = mem.translate(arc.gc_data_addr);
            const uint8_t* host_data = arc.parsed.file_data(*file, host_base,
                                                             arc.gc_data_size);
            if (host_data) {
                uint32_t offset = (uint32_t)(host_data - host_base);
                return arc.gc_data_addr + offset;
            }
        }
    }
    return 0;
}

// ---- Find by Object Address ----

const MountedArchive* find_by_obj(uint32_t gc_jkr_obj_addr) {
    for (auto& arc : g_archives) {
        if (arc.gc_jkr_obj_addr == gc_jkr_obj_addr) return &arc;
    }
    return nullptr;
}

// ---- OS Function Replacements ----
// These override the patched recompiled functions via func_table registration.

// Canonical JKRFileLoader::fetchVolumeName from dusk/zeldaret-tp:
//   - "/" → "/" (root)
//   - "/abc/foo/bar" → "abc" (skip leading slash, lowercase, stop at next '/')
// Returns the parsed name in `out` and the rest of the path (or "/" if no rest).
static std::string fetch_volume_name(const std::string& path) {
    if (path == "/") return "/";
    if (path.empty() || path[0] != '/') return "";
    std::string out;
    for (size_t i = 1; i < path.size() && path[i] != '/'; ++i) {
        out.push_back((char)std::tolower((unsigned char)path[i]));
    }
    return out;
}

// Toggle for the previous behavior (prefix-match + fallback to first archive).
// Default: canonical (exact match, NULL on miss, sCurrentVolume for non-'/').
// Set WW_JKR_LEGACY_FALLBACK=1 to re-enable the old mask.
static bool jkr_legacy_fallback() {
    static const bool b = std::getenv("WW_JKR_LEGACY_FALLBACK") != nullptr;
    return b;
}

// Tracks the most-recently-mounted/found volume to mirror JKRFileLoader::sCurrentVolume.
// Canonical: getGlbResource/findVolume on a path NOT starting with '/' returns sCurrentVolume.
static uint32_t g_current_volume_obj = 0;

// Find archive by exact-match volume name (case-insensitive on the input,
// volume names are already lowercase from fetch_volume_name).
static MountedArchive* find_archive_exact(const std::string& vname) {
    for (auto& arc : g_archives) {
        // arc.volume_name was set at mount time; compare lowercased.
        if (arc.volume_name.size() == vname.size() &&
            _strnicmp(arc.volume_name.c_str(), vname.c_str(),
                      vname.size()) == 0) {
            return &arc;
        }
    }
    return nullptr;
}

// func_802B6FEC: JKRFileLoader::getGlbResource (static)
//   Canonical: parses volume name from path, looks up volume, calls
//   volume->getResource(rest_of_path) which returns the file DATA pointer.
// Our HLE returns the JKR object address (legacy behavior preserved for
// caller compatibility). Real getResource lookup happens in vtable[5].
static void hle_getGlbResource(PPCContext* ctx, Memory* mem) {
    uint32_t path_addr = ctx->r[3];
    std::string path = read_gc_string(*mem, path_addr);

    if (g_archives.empty()) {
        ctx->r[3] = 0;
        return;
    }

    // Canonical algorithm: path not starting with '/' uses sCurrentVolume.
    if (path.empty() || path[0] != '/') {
        if (g_current_volume_obj != 0) {
            printf("[JKR] getGlbResource('%s') -> 0x%08X (sCurrentVolume)\n",
                   path.c_str(), g_current_volume_obj);
            ctx->r[3] = g_current_volume_obj;
            return;
        }
        // No current volume set — legacy fallback or NULL.
        if (jkr_legacy_fallback()) {
            ctx->r[3] = g_archives[0].gc_jkr_obj_addr;
            printf("[JKR] getGlbResource('%s') -> 0x%08X (legacy fallback, no current)\n",
                   path.c_str(), ctx->r[3]);
            return;
        }
        printf("[JKR] getGlbResource('%s') -> NULL (no current volume, no leading slash)\n",
               path.c_str());
        ctx->r[3] = 0;
        return;
    }

    std::string vname = fetch_volume_name(path);
    MountedArchive* arc = find_archive_exact(vname);

    if (arc) {
        uint32_t obj = arc->gc_jkr_obj_addr;
        uint32_t mc = mem->read32(obj + off::MOUNT_COUNT);
        mem->write32(obj + off::MOUNT_COUNT, mc + 1);
        g_current_volume_obj = obj;
        printf("[JKR] getGlbResource('%s') -> 0x%08X (volume='%s')\n",
               path.c_str(), obj, vname.c_str());
        ctx->r[3] = obj;
        return;
    }

    // No match. Canonical: NULL. Legacy: first archive.
    if (jkr_legacy_fallback()) {
        uint32_t obj = g_archives[0].gc_jkr_obj_addr;
        uint32_t mc = mem->read32(obj + off::MOUNT_COUNT);
        mem->write32(obj + off::MOUNT_COUNT, mc + 1);
        printf("[JKR] getGlbResource('%s') vol='%s' -> 0x%08X (LEGACY fallback)\n",
               path.c_str(), vname.c_str(), obj);
        ctx->r[3] = obj;
        return;
    }

    printf("[JKR] getGlbResource('%s') vol='%s' -> NULL (no match)\n",
           path.c_str(), vname.c_str());
    ctx->r[3] = 0;
}

// func_802B6AB8: JKRFileLoader::findVolume (static)
//   Canonical: takes a `const char**` (in-out path pointer). If *path doesn't
//   start with '/', returns sCurrentVolume. Else parses volume name, walks
//   sVolumeList, exact strcmp match. Updates *path to point past the volume
//   segment. Returns NULL on miss.
static void hle_findVolume(PPCContext* ctx, Memory* mem) {
    uint32_t arg = ctx->r[3];
    std::string name;

    // r3 typically points to a `const char**` (in/out path pointer).
    uint32_t maybe_str = mem->read32(arg);
    if (maybe_str >= 0x80000000 && maybe_str < 0x81800000) {
        name = read_gc_string(*mem, maybe_str);
    } else {
        name = read_gc_string(*mem, arg);
    }

    if (name.empty() || name[0] != '/') {
        // Canonical: return sCurrentVolume.
        if (g_current_volume_obj != 0) {
            printf("[JKR] findVolume('%s') -> 0x%08X (sCurrentVolume)\n",
                   name.c_str(), g_current_volume_obj);
            ctx->r[3] = g_current_volume_obj;
            return;
        }
        if (jkr_legacy_fallback() && !g_archives.empty()) {
            ctx->r[3] = g_archives[0].gc_jkr_obj_addr;
            printf("[JKR] findVolume('%s') -> 0x%08X (legacy fallback, no current)\n",
                   name.c_str(), ctx->r[3]);
            return;
        }
        printf("[JKR] findVolume('%s') -> NULL (no current volume)\n",
               name.c_str());
        ctx->r[3] = 0;
        return;
    }

    std::string vname = fetch_volume_name(name);
    MountedArchive* arc = find_archive_exact(vname);

    if (arc) {
        g_current_volume_obj = arc->gc_jkr_obj_addr;
        printf("[JKR] findVolume('%s') -> 0x%08X (volume='%s')\n",
               name.c_str(), arc->gc_jkr_obj_addr, vname.c_str());
        ctx->r[3] = arc->gc_jkr_obj_addr;
        return;
    }

    if (jkr_legacy_fallback() && !g_archives.empty()) {
        printf("[JKR] findVolume('%s') vol='%s' -> 0x%08X (LEGACY fallback)\n",
               name.c_str(), vname.c_str(), g_archives[0].gc_jkr_obj_addr);
        ctx->r[3] = g_archives[0].gc_jkr_obj_addr;
        return;
    }

    printf("[JKR] findVolume('%s') vol='%s' -> NULL (no match)\n",
           name.c_str(), vname.c_str());
    ctx->r[3] = 0;
}

void register_os_funcs(FuncTable& ft, Memory& mem) {
    ft.register_func(0x802B6FEC, hle_getGlbResource);
    ft.register_func(0x802B6AB8, hle_findVolume);
    printf("[JKR] Registered getGlbResource (0x802B6FEC) and findVolume (0x802B6AB8)\n");
}

} // namespace jkr
} // namespace ww
