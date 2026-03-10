// =============================================================================
// Audio System - XAudio2 Backend (Windows)
// Outputs mixed game audio to the speakers
// =============================================================================

#include "ww/audio/audio.h"
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// XAudio2 initialization will go here
// For now, stub implementation
#endif

namespace ww::audio {

static bool g_audio_initialized = false;
static uint32_t g_sample_rate = 32000;

bool audio_init(uint32_t sample_rate, uint32_t channels) {
    g_sample_rate = sample_rate;
    printf("[Audio] Initializing (%u Hz, %u channels)\n", sample_rate, channels);

#ifdef _WIN32
    // TODO: Initialize XAudio2
    // CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // XAudio2Create(...)
    printf("[Audio] XAudio2 backend (stub)\n");
#endif

    g_audio_initialized = true;
    printf("[Audio] Ready. Wind Waker's soundtrack awaits.\n");
    return true;
}

void audio_shutdown() {
    if (g_audio_initialized) {
        // TODO: Cleanup XAudio2
        g_audio_initialized = false;
        printf("[Audio] Shutdown.\n");
    }
}

void audio_update() {
    if (!g_audio_initialized) return;
    // TODO: Fill XAudio2 buffer from mixer
}

} // namespace ww::audio
