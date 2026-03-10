#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Input System
// Maps keyboard/mouse/gamepad to GameCube controller
// The GameCube controller: 2 analog sticks, A/B/X/Y, Z, L/R (analog), D-pad, Start
// =============================================================================

#include <cstdint>

namespace ww::input {

// GameCube controller button bits (matching PADStatus)
enum GCButton : uint16_t {
    PAD_BUTTON_LEFT  = 0x0001,
    PAD_BUTTON_RIGHT = 0x0002,
    PAD_BUTTON_DOWN  = 0x0004,
    PAD_BUTTON_UP    = 0x0008,
    PAD_TRIGGER_Z    = 0x0010,
    PAD_TRIGGER_R    = 0x0020,
    PAD_TRIGGER_L    = 0x0040,
    PAD_BUTTON_A     = 0x0100,
    PAD_BUTTON_B     = 0x0200,
    PAD_BUTTON_X     = 0x0400,
    PAD_BUTTON_Y     = 0x0800,
    PAD_BUTTON_START = 0x1000,
};

// Mirrors the GameCube PADStatus struct
struct PadStatus {
    uint16_t button;        // Button bits
    int8_t   stick_x;       // Main stick X (-128 to 127)
    int8_t   stick_y;       // Main stick Y (-128 to 127)
    int8_t   substick_x;    // C-stick X
    int8_t   substick_y;    // C-stick Y
    uint8_t  trigger_l;     // L trigger (0-255, analog)
    uint8_t  trigger_r;     // R trigger (0-255, analog)
    uint8_t  analog_a;      // A button pressure (not used by most games)
    uint8_t  analog_b;      // B button pressure
    int8_t   err;           // Error status (0 = OK)
};

// Default keyboard mapping for Wind Waker:
//   WASD        = Left stick (Link movement)
//   Arrow keys  = C-stick (camera)
//   Space       = A button (roll, talk, pick up)
//   Shift       = B button (sword)
//   E           = X button (items)
//   Q           = Y button (items)
//   R           = Z button (target/L-trigger)
//   Tab         = R trigger (shield)
//   F           = L trigger
//   Enter       = Start
//   Mouse       = Camera (optional)

bool input_init();
void input_shutdown();
void input_update();

// Get current pad state (controller 0-3, but Wind Waker only uses 0)
PadStatus input_get_pad(int controller = 0);

// Check if a button was just pressed this frame (edge detection)
bool input_pressed(GCButton btn, int controller = 0);

// Check if a button was just released this frame
bool input_released(GCButton btn, int controller = 0);

// Rumble motor control
void input_set_rumble(int controller, bool on);

} // namespace ww::input
