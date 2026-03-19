#pragma once
// =============================================================================
// J3D Binary Model Format Parser (BDL/BMD)
//
// Parses Nintendo's J3D binary model format used by Wind Waker and other
// GameCube/Wii games. Extracts geometry, materials, and textures from BDL
// files for rendering through the GX pipeline.
//
// BDL file structure:
//   J3D header (32 bytes)
//   INF1 — Scene graph hierarchy
//   VTX1 — Vertex position/normal/color/texcoord arrays
//   EVP1 — Envelope (skinning weights)
//   DRW1 — Draw matrix table
//   JNT1 — Joint/bone definitions
//   SHP1 — Shape batches (display lists)
//   MAT3 — Material definitions (TEV, textures, blend)
//   MDL3 — Display list material preloads (BDL only)
//   TEX1 — Texture headers and image data
// =============================================================================

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace ww {
namespace j3d {

// Big-endian read helpers
inline uint16_t read16(const uint8_t* p) { return (p[0] << 8) | p[1]; }
inline uint32_t read32(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
inline int16_t  reads16(const uint8_t* p) { return (int16_t)read16(p); }
inline int32_t  reads32(const uint8_t* p) { return (int32_t)read32(p); }

inline float readf32(const uint8_t* p) {
    uint32_t bits = read32(p);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

// J3D section tags
enum SectionTag : uint32_t {
    TAG_INF1 = 0x494E4631, // 'INF1'
    TAG_VTX1 = 0x56545831, // 'VTX1'
    TAG_EVP1 = 0x45565031, // 'EVP1'
    TAG_DRW1 = 0x44525731, // 'DRW1'
    TAG_JNT1 = 0x4A4E5431, // 'JNT1'
    TAG_SHP1 = 0x53485031, // 'SHP1'
    TAG_MAT3 = 0x4D415433, // 'MAT3'
    TAG_MDL3 = 0x4D444C33, // 'MDL3'
    TAG_TEX1 = 0x54455831, // 'TEX1'
};

// GX vertex attribute types (matching GX API)
enum GXAttr : uint8_t {
    GX_VA_PNMTXIDX  = 0,
    GX_VA_TEX0MTXIDX = 1,
    GX_VA_POS       = 9,
    GX_VA_NRM       = 10,
    GX_VA_CLR0      = 11,
    GX_VA_CLR1      = 12,
    GX_VA_TEX0      = 13,
    GX_VA_TEX7      = 20,
    GX_VA_NULL      = 0xFF,
};

// GX component types
enum GXCompType : uint8_t {
    GX_U8  = 0, GX_S8  = 1,
    GX_U16 = 2, GX_S16 = 3,
    GX_F32 = 4,
    // Color types
    GX_RGB565  = 0, GX_RGB8  = 1,
    GX_RGBX8   = 2, GX_RGBA4 = 3,
    GX_RGBA6   = 4, GX_RGBA8 = 5,
};

// GX primitive types
enum GXPrimitive : uint8_t {
    GX_QUADS         = 0x80,
    GX_TRIANGLES     = 0x90,
    GX_TRIANGLESTRIP = 0x98,
    GX_TRIANGLEFAN   = 0xA0,
    GX_LINES         = 0xA8,
    GX_LINESTRIP     = 0xB0,
    GX_POINTS        = 0xB8,
};

// Vertex array descriptor (from VTX1)
struct VertexArray {
    uint32_t attr;          // GXAttr
    uint32_t comp_type;     // GXCompType
    uint32_t comp_count;    // Number of components (e.g., 3 for XYZ)
    uint32_t frac_bits;     // Fixed-point fractional bits (for integer types)
    const uint8_t* data;    // Pointer into BDL data
    uint32_t data_size;     // Size of array data in bytes
    uint32_t stride;        // Bytes per element
    uint32_t count;         // Number of elements
};

// Shape batch (from SHP1) — a draw call
struct ShapeBatch {
    uint8_t  matrix_type;   // 0=none, 1=billboard, 2=Y-billboard
    uint16_t packet_count;
    // Each packet contains a display list with GX primitives
    struct Packet {
        const uint8_t* display_list;
        uint32_t       display_list_size;
    };
    std::vector<Packet> packets;

    // Vertex attribute table for this batch
    struct AttrEntry {
        uint32_t attr;      // GXAttr
        uint32_t data_type; // 0=none, 1=direct, 2=index8, 3=index16
    };
    std::vector<AttrEntry> attribs;
};

// Texture header (from TEX1)
struct TextureHeader {
    std::string name;
    uint8_t  format;        // GX texture format
    uint16_t width;
    uint16_t height;
    uint8_t  wrap_s;
    uint8_t  wrap_t;
    uint8_t  min_filter;
    uint8_t  mag_filter;
    uint8_t  mipmap_count;
    const uint8_t* image_data;
    uint32_t image_size;
};

// Parsed J3D model
struct J3DModel {
    // File info
    std::string type;       // "bmd3" or "bdl4"
    uint32_t    file_size;
    uint32_t    section_count;

    // VTX1 — vertex arrays
    std::vector<VertexArray> vertex_arrays;

    // SHP1 — shape batches
    std::vector<ShapeBatch> shapes;

    // TEX1 — textures
    std::vector<TextureHeader> textures;

    // INF1 — shape-to-material mapping
    // shape_material[shape_index] = material_index
    std::vector<int> shape_material;

    // Section offsets (for debugging)
    struct SectionInfo {
        uint32_t tag;
        uint32_t offset;
        uint32_t size;
    };
    std::vector<SectionInfo> sections;

    // Stats
    uint32_t total_vertices() const;
    uint32_t total_triangles_approx() const;
};

// Parse a BDL/BMD file from raw data.
// Returns true on success. The data pointer must remain valid for the lifetime
// of the returned model (vertex/texture data points into the original buffer).
bool j3d_parse(const uint8_t* data, size_t size, J3DModel& out);

// Print a summary of the parsed model to stdout.
void j3d_print_summary(const J3DModel& model);

} // namespace j3d
} // namespace ww
