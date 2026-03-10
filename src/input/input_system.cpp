// =============================================================================
// Input System - Keyboard/Mouse/Gamepad to GameCube Controller
// Maps modern input to the GameCube's controller layout
// Wind Waker only uses controller port 0
// =============================================================================

#include "ww/input/input.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#pragma comment(lib, "xinput.lib")
#endif

namespace ww::input {

static PadStatus g_pad[4];
static PadStatus g_pad_prev[4];
static bool g_initialized = false;

bool input_init() {
    memset(g_pad, 0, sizeof(g_pad));
    memset(g_pad_prev, 0, sizeof(g_pad_prev));
    g_initialized = true;
    printf("[Input] Initialized.\n");
    printf("[Input] Controls:\n");
    printf("  WASD          - Move Link\n");
    printf("  Mouse/Arrows  - Camera\n");
    printf("  Space         - A (Roll/Talk/Grab)\n");
    printf("  Left Shift    - B (Sword)\n");
    printf("  E             - X (Item 1)\n");
    printf("  Q             - Y (Item 2)\n");
    printf("  R             - Z (Target)\n");
    printf("  Tab           - R (Shield)\n");
    printf("  F             - L\n");
    printf("  Enter         - Start\n");
    printf("  XInput gamepad also supported.\n");
    return true;
}

void input_shutdown() {
    g_initialized = false;
}

void input_update() {
    if (!g_initialized) return;

    memcpy(g_pad_prev, g_pad, sizeof(g_pad));
    memset(&g_pad[0], 0, sizeof(PadStatus));

#ifdef _WIN32
    // ---- Keyboard input ----
    auto key_down = [](int vk) -> bool {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    };

    // Buttons
    if (key_down(VK_SPACE))   g_pad[0].button |= PAD_BUTTON_A;
    if (key_down(VK_SHIFT))   g_pad[0].button |= PAD_BUTTON_B;
    if (key_down('E'))        g_pad[0].button |= PAD_BUTTON_X;
    if (key_down('Q'))        g_pad[0].button |= PAD_BUTTON_Y;
    if (key_down('R'))        g_pad[0].button |= PAD_TRIGGER_Z;
    if (key_down(VK_TAB))     g_pad[0].button |= PAD_TRIGGER_R;
    if (key_down('F'))        g_pad[0].button |= PAD_TRIGGER_L;
    if (key_down(VK_RETURN))  g_pad[0].button |= PAD_BUTTON_START;

    // Main stick (WASD)
    int8_t sx = 0, sy = 0;
    if (key_down('W')) sy += 80;
    if (key_down('S')) sy -= 80;
    if (key_down('A')) sx -= 80;
    if (key_down('D')) sx += 80;
    g_pad[0].stick_x = sx;
    g_pad[0].stick_y = sy;

    // C-stick (Arrow keys)
    int8_t cx = 0, cy = 0;
    if (key_down(VK_UP))    cy += 80;
    if (key_down(VK_DOWN))  cy -= 80;
    if (key_down(VK_LEFT))  cx -= 80;
    if (key_down(VK_RIGHT)) cx += 80;
    g_pad[0].substick_x = cx;
    g_pad[0].substick_y = cy;

    // Analog triggers
    if (key_down(VK_TAB)) g_pad[0].trigger_r = 200;
    if (key_down('F'))    g_pad[0].trigger_l = 200;

    // ---- XInput gamepad (if connected) ----
    XINPUT_STATE xi_state;
    if (XInputGetState(0, &xi_state) == ERROR_SUCCESS) {
        auto& gp = xi_state.Gamepad;

        if (gp.wButtons & XINPUT_GAMEPAD_A)          g_pad[0].button |= PAD_BUTTON_A;
        if (gp.wButtons & XINPUT_GAMEPAD_B)          g_pad[0].button |= PAD_BUTTON_B;
        if (gp.wButtons & XINPUT_GAMEPAD_X)          g_pad[0].button |= PAD_BUTTON_X;
        if (gp.wButtons & XINPUT_GAMEPAD_Y)          g_pad[0].button |= PAD_BUTTON_Y;
        if (gp.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  g_pad[0].button |= PAD_TRIGGER_L;
        if (gp.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) g_pad[0].button |= PAD_TRIGGER_Z;
        if (gp.wButtons & XINPUT_GAMEPAD_START)      g_pad[0].button |= PAD_BUTTON_START;
        if (gp.wButtons & XINPUT_GAMEPAD_DPAD_UP)    g_pad[0].button |= PAD_BUTTON_UP;
        if (gp.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  g_pad[0].button |= PAD_BUTTON_DOWN;
        if (gp.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  g_pad[0].button |= PAD_BUTTON_LEFT;
        if (gp.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) g_pad[0].button |= PAD_BUTTON_RIGHT;

        // Analog sticks (map +-32768 to +-127)
        g_pad[0].stick_x = (int8_t)(gp.sThumbLX / 256);
        g_pad[0].stick_y = (int8_t)(gp.sThumbLY / 256);
        g_pad[0].substick_x = (int8_t)(gp.sThumbRX / 256);
        g_pad[0].substick_y = (int8_t)(gp.sThumbRY / 256);

        // Triggers
        g_pad[0].trigger_l = gp.bLeftTrigger;
        g_pad[0].trigger_r = gp.bRightTrigger;
        if (gp.bLeftTrigger > 100)  g_pad[0].button |= PAD_TRIGGER_L;
        if (gp.bRightTrigger > 100) g_pad[0].button |= PAD_TRIGGER_R;
    }
#endif
}

PadStatus input_get_pad(int controller) {
    if (controller >= 0 && controller < 4) return g_pad[controller];
    PadStatus empty = {};
    return empty;
}

bool input_pressed(GCButton btn, int controller) {
    if (controller < 0 || controller >= 4) return false;
    return (g_pad[controller].button & btn) && !(g_pad_prev[controller].button & btn);
}

bool input_released(GCButton btn, int controller) {
    if (controller < 0 || controller >= 4) return false;
    return !(g_pad[controller].button & btn) && (g_pad_prev[controller].button & btn);
}

void input_set_rumble(int controller, bool on) {
#ifdef _WIN32
    XINPUT_VIBRATION vib = {};
    if (on) {
        vib.wLeftMotorSpeed = 32000;
        vib.wRightMotorSpeed = 16000;
    }
    XInputSetState(controller, &vib);
#endif
}

} // namespace ww::input
