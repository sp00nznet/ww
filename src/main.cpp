// =============================================================================
// Wind Waker Static Recompilation - Main Launcher
//
// "The wind... it is blowing."
//
// This is the entry point for the recompiled game.
// It initializes all subsystems, loads the game data, and enters the
// main loop. Link's adventure on the Great Sea, running natively on
// Windows 11 — no emulator in sight.
// =============================================================================

#include "ww/runtime.h"
#include "ww/gx/gx.h"
#include "ww/audio/audio.h"
#include "ww/input/input.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using namespace ww;

static const uint32_t WINDOW_WIDTH  = 1280;
static const uint32_t WINDOW_HEIGHT = 720;

static void print_banner() {
    printf("\n");
    printf("  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("      _____ _          __      ___           _\n");
    printf("     |_   _| |_  ___  \\ \\    / (_)_ _  __| |\n");
    printf("       | | | ' \\/ -_)  \\ \\/\\/ /| | ' \\/ _` |\n");
    printf("       |_| |_||_\\___|  _\\_/\\_/ |_|_||_\\__,_|\n");
    printf("         \\ \\    / /_ _| |_____ _ _\n");
    printf("          \\ \\/\\/ / _` | / / -_) '_|\n");
    printf("           \\_/\\_/\\__,_|_\\_\\___|_|\n");
    printf("\n");
    printf("       Static Recompilation - Native Windows 11\n");
    printf("       GameCube PowerPC 750 (Gekko) -> x86-64\n");
    printf("       No emulator. Just the wind at your back.\n");
    printf("  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("\n");
}

int main(int argc, char** argv) {
    print_banner();

    // ---- Initialize Runtime ----
    printf("[*] Initializing runtime...\n");
    if (!runtime_init()) {
        fprintf(stderr, "Failed to initialize runtime\n");
        return 1;
    }

    // ---- Initialize Graphics ----
    printf("[*] Initializing graphics (D3D11)...\n");
    gx::GXInit();
    if (!gx::GXInitBackend(nullptr, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        fprintf(stderr, "Failed to initialize D3D11 backend\n");
        runtime_shutdown();
        return 1;
    }

    // ---- Initialize Audio ----
    printf("[*] Initializing audio...\n");
    audio::audio_init(32000, 2);

    // ---- Initialize Input ----
    printf("[*] Initializing input...\n");
    input::input_init();

    // ---- Main Loop ----
    printf("[*] Entering main loop. The Great Sea awaits!\n");
    printf("[*] (Press ESC to quit)\n\n");

    bool running = true;
    uint32_t frame = 0;

#ifdef _WIN32
    while (running) {
        // Process Windows messages
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        // Update input
        input::input_update();

        // Update audio
        audio::audio_update();

        // TODO: Call recompiled game main loop
        // The game's main function will be:
        //   func_80003100(&g_ctx, &g_mem);  // Entry point
        // Which calls through the game's init -> main loop -> render chain

        // Present frame
        gx::GXPresent();

        frame++;
    }
#endif

    // ---- Cleanup ----
    printf("\n[*] Shutting down...\n");
    input::input_shutdown();
    audio::audio_shutdown();
    gx::GXShutdownBackend();
    runtime_shutdown();

    printf("[*] Thanks for sailing the Great Sea! See you next time.\n");
    return 0;
}
