// =============================================================================
// J3D Binary Model Format Parser (BDL/BMD)
// =============================================================================

#include "ww/j3d.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ww {
namespace j3d {

// Compute stride for a vertex array element
static uint32_t compute_stride(uint32_t attr, uint32_t comp_type, uint32_t comp_count) {
    // Color attributes have special sizing
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
        switch (comp_type) {
            case GX_RGB565: return 2;
            case GX_RGB8:   return 3;
            case GX_RGBX8:  return 4;
            case GX_RGBA4:  return 2;
            case GX_RGBA6:  return 3;
            case GX_RGBA8:  return 4;
            default:        return 4;
        }
    }
    // Normal attributes
    uint32_t comp_size = 0;
    switch (comp_type) {
        case GX_U8:  case GX_S8:  comp_size = 1; break;
        case GX_U16: case GX_S16: comp_size = 2; break;
        case GX_F32:              comp_size = 4; break;
        default:                  comp_size = 4; break;
    }
    return comp_count * comp_size;
}

// Parse VTX1 section — vertex arrays
static bool parse_vtx1(const uint8_t* section, uint32_t section_size, J3DModel& model) {
    if (section_size < 0x40) return false;

    // VTX1 header:
    //   +0x08: array format offset (relative to section start)
    //   +0x0C: array data offsets table (13 entries, one per attr type)
    uint32_t fmt_offset = read32(section + 0x08);
    uint32_t data_offsets_offset = read32(section + 0x0C);

    // Read array data offsets (13 entries for position through texcoord7)
    uint32_t data_offsets[13] = {};
    if (data_offsets_offset + 13 * 4 <= section_size) {
        for (int i = 0; i < 13; i++) {
            data_offsets[i] = read32(section + data_offsets_offset + i * 4);
        }
    }

    // Parse vertex attribute format entries
    // Each entry: 4 bytes — attr(u32), comp_count(u32), comp_type(u32), frac_bits(u8), pad(3)
    // Actually: attr(u32), cnt(u32), type(u32), frac(u8), pad[3]
    // Wait — the J3D VTX1 format entry is:
    //   u32 attr_type, u32 comp_cnt, u32 comp_type, u8 frac_bits, u8 pad[3]
    // Total: 16 bytes per entry, terminated by attr_type == 0xFF
    uint32_t fmt_pos = fmt_offset;
    while (fmt_pos + 16 <= section_size) {
        uint32_t attr = read32(section + fmt_pos);
        if (attr == 0xFF || attr > 20) break;

        uint32_t comp_count = read32(section + fmt_pos + 4);
        uint32_t comp_type  = read32(section + fmt_pos + 8);
        uint8_t  frac_bits  = section[fmt_pos + 12];

        // Map attr to data offset index
        // Attrs: POS=9, NRM=10, CLR0=11, CLR1=12, TEX0=13..TEX7=20
        // Data offsets array: [0]=pos, [1]=nrm, [2]=clr0, [3]=clr1, [4-11]=tex0-7
        int data_idx = -1;
        if (attr == GX_VA_POS)  data_idx = 0;
        else if (attr == GX_VA_NRM)  data_idx = 1;
        else if (attr == GX_VA_CLR0) data_idx = 2;
        else if (attr == GX_VA_CLR1) data_idx = 3;
        else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) data_idx = 4 + (attr - GX_VA_TEX0);

        if (data_idx >= 0 && data_idx < 13 && data_offsets[data_idx] != 0) {
            VertexArray va = {};
            va.attr = attr;
            va.comp_type = comp_type;
            va.comp_count = comp_count + 1; // J3D stores count-1 (0=1 component, 1=2, 2=3)
            // For position: comp_count 0=XY(2), 1=XYZ(3)
            // For normal: comp_count 0=XYZ(3) (always 3)
            // For texcoord: comp_count 0=S(1), 1=ST(2)
            if (attr == GX_VA_POS) va.comp_count = (comp_count == 0) ? 2 : 3;
            else if (attr == GX_VA_NRM) va.comp_count = 3;
            else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) va.comp_count = (comp_count == 0) ? 1 : 2;
            va.frac_bits = frac_bits;
            va.stride = compute_stride(attr, comp_type, va.comp_count);

            uint32_t arr_off = data_offsets[data_idx];
            va.data = section + arr_off;

            // Calculate data size: extends to the next array or end of section
            uint32_t next_off = section_size;
            for (int j = 0; j < 13; j++) {
                if (data_offsets[j] > arr_off && data_offsets[j] < next_off) {
                    next_off = data_offsets[j];
                }
            }
            va.data_size = next_off - arr_off;
            va.count = (va.stride > 0) ? va.data_size / va.stride : 0;

            model.vertex_arrays.push_back(va);
        }

        fmt_pos += 16;
    }

    return true;
}

// Parse SHP1 section — shape batches
static bool parse_shp1(const uint8_t* section, uint32_t section_size, J3DModel& model) {
    if (section_size < 0x2C) return false;

    uint16_t batch_count = read16(section + 0x08);
    uint32_t batch_offset = read32(section + 0x0C);
    // +0x10: padding
    uint32_t attrib_offset = read32(section + 0x14);
    uint32_t mtx_table_offset = read32(section + 0x18);
    uint32_t dl_data_offset = read32(section + 0x1C); // display list data
    uint32_t mtx_data_offset = read32(section + 0x20);
    uint32_t packet_info_offset = read32(section + 0x24);

    // Each batch entry: 40 bytes
    for (uint16_t i = 0; i < batch_count; i++) {
        uint32_t bo = batch_offset + i * 40;
        if (bo + 40 > section_size) break;

        ShapeBatch batch = {};
        batch.matrix_type = section[bo + 0];
        batch.packet_count = read16(bo + section + 2);
        uint16_t attrib_table_offset = read16(section + bo + 4);
        uint16_t first_mtx_data = read16(section + bo + 6);
        uint16_t first_packet = read16(section + bo + 8);
        // +10: padding (6 bytes)
        // +16: bounding box (24 bytes: float min[3], max[3])

        // Parse attribute table for this batch
        uint32_t attr_pos = attrib_offset + attrib_table_offset;
        while (attr_pos + 4 <= section_size) {
            uint32_t a = read32(section + attr_pos);
            if (a == 0xFF || (a & 0xFF) == 0xFF) break;
            // Each entry: u32 attr, u32 data_type
            uint32_t data_type = read32(section + attr_pos + 4);
            ShapeBatch::AttrEntry ae;
            ae.attr = a;
            ae.data_type = data_type;
            batch.attribs.push_back(ae);
            attr_pos += 8;
        }

        // Parse packets (display lists)
        for (uint16_t p = 0; p < batch.packet_count; p++) {
            uint32_t pi = packet_info_offset + (first_packet + p) * 8;
            if (pi + 8 > section_size) break;

            uint32_t dl_size = read32(section + pi + 0);
            uint32_t dl_off  = read32(section + pi + 4);

            ShapeBatch::Packet pkt;
            pkt.display_list = section + dl_data_offset + dl_off;
            pkt.display_list_size = dl_size;
            batch.packets.push_back(pkt);
        }

        model.shapes.push_back(std::move(batch));
    }

    return true;
}

// Parse TEX1 section — texture headers
static bool parse_tex1(const uint8_t* section, uint32_t section_size, J3DModel& model) {
    if (section_size < 0x10) return false;

    uint16_t tex_count = read16(section + 0x08);
    uint32_t header_offset = read32(section + 0x0C);
    uint32_t string_table_offset = read32(section + 0x10);

    // Parse string table
    std::vector<std::string> names;
    if (string_table_offset < section_size) {
        uint16_t str_count = read16(section + string_table_offset);
        // +2: padding
        // Then str_count entries: u16 hash, u16 offset
        for (uint16_t i = 0; i < str_count; i++) {
            uint32_t entry_off = string_table_offset + 4 + i * 4;
            if (entry_off + 4 > section_size) break;
            uint16_t str_off = read16(section + entry_off + 2);
            uint32_t abs_off = string_table_offset + str_off;
            if (abs_off < section_size) {
                const char* s = (const char*)(section + abs_off);
                size_t max_len = section_size - abs_off;
                names.push_back(std::string(s, strnlen(s, max_len)));
            }
        }
    }

    // Each texture header: 32 bytes
    for (uint16_t i = 0; i < tex_count; i++) {
        uint32_t to = header_offset + i * 32;
        if (to + 32 > section_size) break;

        TextureHeader tex = {};
        tex.format = section[to + 0];
        // +1: alpha flag
        tex.width  = read16(section + to + 2);
        tex.height = read16(section + to + 4);
        tex.wrap_s = section[to + 6];
        tex.wrap_t = section[to + 7];
        // +8: palette format
        // +9: palette count (u16)
        // +12: palette offset (u32)
        tex.min_filter = section[to + 14]; // actually at different offset
        tex.mag_filter = section[to + 15];
        // +16: min LOD, max LOD
        tex.mipmap_count = section[to + 18];
        // +20: image data offset (relative to texture header start)
        uint32_t img_offset = read32(section + to + 28);
        tex.image_data = section + to + img_offset;
        // Estimate image size from format and dimensions
        // For now just store a rough estimate
        tex.image_size = tex.width * tex.height; // approximate

        if (i < names.size()) {
            tex.name = names[i];
        }

        model.textures.push_back(std::move(tex));
    }

    return true;
}

// Main parser
bool j3d_parse(const uint8_t* data, size_t size, J3DModel& out) {
    if (size < 32) return false;

    // J3D header: 8 bytes magic, 4 bytes file size, 4 bytes section count
    //             then 16 bytes SVR3 sub-header (or padding)
    char magic[9] = {};
    memcpy(magic, data, 8);
    out.type = std::string(magic, 8);

    // Check for valid J3D magic
    if (memcmp(data, "J3D2bdl4", 8) != 0 &&
        memcmp(data, "J3D2bmd3", 8) != 0) {
        fprintf(stderr, "[J3D] Unknown magic: %.8s\n", data);
        return false;
    }

    out.file_size = read32(data + 8);
    out.section_count = read32(data + 12);

    // Iterate sections starting at offset 32
    uint32_t pos = 32;
    while (pos + 8 <= size) {
        uint32_t tag = read32(data + pos);
        uint32_t sec_size = read32(data + pos + 4);
        if (sec_size < 8 || pos + sec_size > size) break;

        J3DModel::SectionInfo info;
        info.tag = tag;
        info.offset = pos;
        info.size = sec_size;
        out.sections.push_back(info);

        const uint8_t* sec_data = data + pos;

        switch (tag) {
            case TAG_VTX1:
                parse_vtx1(sec_data, sec_size, out);
                break;
            case TAG_SHP1:
                parse_shp1(sec_data, sec_size, out);
                break;
            case TAG_TEX1:
                parse_tex1(sec_data, sec_size, out);
                break;
            default:
                break;
        }

        pos += sec_size;
        // Align to 32 bytes
        pos = (pos + 31) & ~31;
    }

    return true;
}

uint32_t J3DModel::total_vertices() const {
    uint32_t total = 0;
    for (const auto& va : vertex_arrays) {
        if (va.attr == GX_VA_POS) {
            total += va.count;
        }
    }
    return total;
}

uint32_t J3DModel::total_triangles_approx() const {
    // Rough estimate from display list sizes
    uint32_t total_dl_bytes = 0;
    for (const auto& shape : shapes) {
        for (const auto& pkt : shape.packets) {
            total_dl_bytes += pkt.display_list_size;
        }
    }
    // Very rough: ~10 bytes per triangle on average
    return total_dl_bytes / 10;
}

static const char* attr_name(uint32_t attr) {
    switch (attr) {
        case GX_VA_PNMTXIDX: return "PnMtxIdx";
        case GX_VA_TEX0MTXIDX: return "Tex0MtxIdx";
        case GX_VA_POS: return "Position";
        case GX_VA_NRM: return "Normal";
        case GX_VA_CLR0: return "Color0";
        case GX_VA_CLR1: return "Color1";
        case 13: return "TexCoord0";
        case 14: return "TexCoord1";
        case 15: return "TexCoord2";
        case 16: return "TexCoord3";
        case 17: return "TexCoord4";
        case 18: return "TexCoord5";
        case 19: return "TexCoord6";
        case 20: return "TexCoord7";
        default: return "Unknown";
    }
}

static const char* comp_type_name(uint32_t type) {
    switch (type) {
        case GX_U8:  return "u8";
        case GX_S8:  return "s8";
        case GX_U16: return "u16";
        case GX_S16: return "s16";
        case GX_F32: return "f32";
        default:     return "?";
    }
}

static const char* gx_tex_fmt_name(uint8_t fmt) {
    switch (fmt) {
        case 0: return "I4";
        case 1: return "I8";
        case 2: return "IA4";
        case 3: return "IA8";
        case 4: return "RGB565";
        case 5: return "RGB5A3";
        case 6: return "RGBA8";
        case 8: return "CI4";
        case 9: return "CI8";
        case 10: return "CI14x2";
        case 14: return "CMPR";
        default: return "?";
    }
}

void j3d_print_summary(const J3DModel& model) {
    printf("[J3D] Type: %.8s  Size: %u bytes  Sections: %u\n",
           model.type.c_str(), model.file_size, model.section_count);

    // Section list
    for (const auto& s : model.sections) {
        char tag[5] = {
            (char)((s.tag >> 24) & 0xFF),
            (char)((s.tag >> 16) & 0xFF),
            (char)((s.tag >> 8) & 0xFF),
            (char)(s.tag & 0xFF), 0
        };
        printf("[J3D]   Section '%s' at 0x%04X (%u bytes)\n", tag, s.offset, s.size);
    }

    // Vertex arrays
    if (!model.vertex_arrays.empty()) {
        printf("[J3D]   Vertex arrays: %zu\n", model.vertex_arrays.size());
        for (const auto& va : model.vertex_arrays) {
            printf("[J3D]     %s: %u elements, %u components, %s, stride=%u\n",
                   attr_name(va.attr), va.count, va.comp_count,
                   comp_type_name(va.comp_type), va.stride);
        }
        printf("[J3D]   Total vertices: %u\n", model.total_vertices());
    }

    // Shapes
    if (!model.shapes.empty()) {
        printf("[J3D]   Shape batches: %zu\n", model.shapes.size());
        uint32_t total_dl = 0;
        for (const auto& s : model.shapes) {
            for (const auto& p : s.packets) total_dl += p.display_list_size;
        }
        printf("[J3D]   Total display list data: %u bytes\n", total_dl);
        printf("[J3D]   Estimated triangles: ~%u\n", model.total_triangles_approx());
    }

    // Textures
    if (!model.textures.empty()) {
        printf("[J3D]   Textures: %zu\n", model.textures.size());
        for (const auto& t : model.textures) {
            printf("[J3D]     %s: %ux%u %s (mipmaps=%u)\n",
                   t.name.empty() ? "(unnamed)" : t.name.c_str(),
                   t.width, t.height, gx_tex_fmt_name(t.format), t.mipmap_count);
        }
    }
}

} // namespace j3d
} // namespace ww
