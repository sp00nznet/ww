// =============================================================================
// Audio Mixer
// Mixes multiple audio voices into the output buffer
// Wind Waker's JAudio engine runs dozens of simultaneous sounds:
// ocean waves, seagulls, wind, sword swings, music...
// =============================================================================

#include "ww/audio/audio.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ww::audio {

static constexpr int MAX_VOICES = 64;
static AudioVoice g_voices[MAX_VOICES];

int audio_play(const int16_t* samples, uint32_t count, float volume, float pan) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].playing) {
            g_voices[i].samples = const_cast<int16_t*>(samples);
            g_voices[i].num_samples = count;
            g_voices[i].position = 0;
            g_voices[i].volume = volume;
            g_voices[i].pan = pan;
            g_voices[i].pitch = 1.0f;
            g_voices[i].looping = false;
            g_voices[i].playing = true;
            return i;
        }
    }
    return -1; // No free voices
}

void audio_stop(int voice_id) {
    if (voice_id >= 0 && voice_id < MAX_VOICES) {
        g_voices[voice_id].playing = false;
    }
}

void audio_set_volume(int voice_id, float volume) {
    if (voice_id >= 0 && voice_id < MAX_VOICES) {
        g_voices[voice_id].volume = volume;
    }
}

void audio_set_pan(int voice_id, float pan) {
    if (voice_id >= 0 && voice_id < MAX_VOICES) {
        g_voices[voice_id].pan = std::clamp(pan, -1.0f, 1.0f);
    }
}

void audio_set_pitch(int voice_id, float pitch) {
    if (voice_id >= 0 && voice_id < MAX_VOICES) {
        g_voices[voice_id].pitch = pitch;
    }
}

// Mix all active voices into a stereo output buffer
void mix_voices(float* output_l, float* output_r, uint32_t frame_count) {
    memset(output_l, 0, frame_count * sizeof(float));
    memset(output_r, 0, frame_count * sizeof(float));

    for (int v = 0; v < MAX_VOICES; v++) {
        auto& voice = g_voices[v];
        if (!voice.playing || !voice.samples) continue;

        // Pan: -1 = left, 0 = center, 1 = right
        float vol_l = voice.volume * std::min(1.0f, 1.0f - voice.pan);
        float vol_r = voice.volume * std::min(1.0f, 1.0f + voice.pan);

        for (uint32_t i = 0; i < frame_count; i++) {
            if (voice.position >= voice.num_samples) {
                if (voice.looping) {
                    voice.position = voice.loop_start;
                } else {
                    voice.playing = false;
                    break;
                }
            }

            float sample = voice.samples[voice.position] / 32768.0f;
            output_l[i] += sample * vol_l;
            output_r[i] += sample * vol_r;

            // Advance position by pitch rate
            voice.position += (uint32_t)(voice.pitch); // Simplified - no fractional
        }
    }
}

} // namespace ww::audio
