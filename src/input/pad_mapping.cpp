// =============================================================================
// PAD Mapping - GX PAD API Implementation
// Intercepts game code's calls to PADRead and returns our input state
// =============================================================================

#include "ww/input/input.h"
#include "ww/runtime.h"
#include <cstring>

namespace ww::input {

// Called by recompiled game code via OS function replacement
// Writes PADStatus struct to GameCube memory at the address in r3
void pad_read_hook(ww::PPCContext* ctx, ww::Memory* mem) {
    input_update();

    // r3 = pointer to PADStatus[4] array
    uint32_t status_addr = ctx->r[3];

    for (int i = 0; i < 4; i++) {
        PadStatus pad = input_get_pad(i);
        uint32_t addr = status_addr + i * 12; // PADStatus is 12 bytes

        mem->write16(addr + 0, pad.button);
        mem->write8(addr + 2, (uint8_t)pad.stick_x);
        mem->write8(addr + 3, (uint8_t)pad.stick_y);
        mem->write8(addr + 4, (uint8_t)pad.substick_x);
        mem->write8(addr + 5, (uint8_t)pad.substick_y);
        mem->write8(addr + 6, pad.trigger_l);
        mem->write8(addr + 7, pad.trigger_r);
        mem->write8(addr + 8, pad.analog_a);
        mem->write8(addr + 9, pad.analog_b);
        mem->write8(addr + 10, (uint8_t)pad.err);
        mem->write8(addr + 11, 0); // padding
    }

    // Return value: bitmask of connected controllers
    ctx->r[3] = 0x1; // Only controller 0 connected
}

} // namespace ww::input
