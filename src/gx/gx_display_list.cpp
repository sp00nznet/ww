// =============================================================================
// GX Display List Processing
// GameCube display lists are precompiled command buffers that can be
// "called" to replay a sequence of GX commands efficiently.
// Wind Waker uses these for static geometry and repeated draw calls.
// =============================================================================

#include "ww/gx/gx.h"
#include "ww/runtime.h"
#include <cstdio>

namespace ww::gx {

// GX FIFO command opcodes
enum GXFIFOCmd : uint8_t {
    GX_CMD_NOP       = 0x00,
    GX_CMD_LOAD_CP   = 0x08,
    GX_CMD_LOAD_XF   = 0x10,
    GX_CMD_LOAD_IDX_A = 0x20,
    GX_CMD_LOAD_IDX_B = 0x28,
    GX_CMD_LOAD_IDX_C = 0x30,
    GX_CMD_LOAD_IDX_D = 0x38,
    GX_CMD_LOAD_BP   = 0x61,
    GX_CMD_DRAW_QUADS         = 0x80,
    GX_CMD_DRAW_TRIANGLES     = 0x90,
    GX_CMD_DRAW_TRIANGLESTRIP = 0x98,
    GX_CMD_DRAW_TRIANGLEFAN   = 0xA0,
    GX_CMD_DRAW_LINES         = 0xA8,
    GX_CMD_DRAW_LINESTRIP     = 0xB0,
    GX_CMD_DRAW_POINTS        = 0xB8,
};

void GXCallDisplayList(const uint8_t* data, uint32_t size) {
    uint32_t pos = 0;

    while (pos < size) {
        uint8_t cmd = data[pos++];

        if (cmd == GX_CMD_NOP) continue;

        if (cmd >= 0x80) {
            // Draw command
            // Bits 4-2 = primitive type, bits 7-5 = vertex format index
            uint16_t vtx_count = ((uint16_t)data[pos] << 8) | data[pos + 1];
            pos += 2;

            // TODO: Process vertex data based on current VCD/VAT settings
            // For now, skip the vertex data
            // The actual vertex size depends on the current attribute configuration
            break; // Can't accurately skip without knowing vertex size
        }

        switch (cmd) {
        case GX_CMD_LOAD_BP: {
            uint32_t val = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) |
                           ((uint32_t)data[pos+2] << 8) | data[pos+3];
            pos += 4;
            // TODO: Process BP (blitting processor) register write
            break;
        }
        case GX_CMD_LOAD_CP: {
            uint8_t reg = data[pos++];
            uint32_t val = ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos+1] << 16) |
                           ((uint32_t)data[pos+2] << 8) | data[pos+3];
            pos += 4;
            // TODO: Process CP (command processor) register write
            break;
        }
        case GX_CMD_LOAD_XF: {
            uint16_t length_minus_1 = ((uint16_t)data[pos] << 8) | data[pos + 1];
            uint16_t addr = ((uint16_t)data[pos + 2] << 8) | data[pos + 3];
            pos += 4;
            pos += (length_minus_1 + 1) * 4; // Skip XF data
            break;
        }
        default:
            // Unknown command, bail
            printf("[GX] Unknown display list command: 0x%02X at offset %u\n", cmd, pos - 1);
            return;
        }
    }
}

} // namespace ww::gx
