#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Audio System
// GameCube audio: 16-bit ADPCM at 32kHz via dedicated DSP
// Wind Waker uses the JAudio engine (JAZelAudio)
// =============================================================================

#include <cstdint>
#include <vector>
#include <string>

namespace ww::audio {

// GameCube DSP ADPCM format
struct DSPADPCMInfo {
    int16_t coef[16];      // 8 pairs of ADPCM coefficients
    uint16_t gain;
    uint16_t pred_scale;
    int16_t yn1;           // Previous samples for prediction
    int16_t yn2;
    uint16_t loop_pred_scale;
    int16_t loop_yn1;
    int16_t loop_yn2;
};

// Decode DSP ADPCM to PCM16
std::vector<int16_t> decode_dsp_adpcm(const uint8_t* data, uint32_t num_samples,
                                       const DSPADPCMInfo& info, bool loop = false);

// Audio voice (a playing sound)
struct AudioVoice {
    int16_t* samples;
    uint32_t num_samples;
    uint32_t position;
    float    volume;
    float    pan;           // -1.0 left, 0.0 center, 1.0 right
    float    pitch;         // Playback rate multiplier
    bool     looping;
    bool     playing;
    uint32_t loop_start;
    uint32_t loop_end;
};

// Initialize audio system (XAudio2 on Windows)
bool audio_init(uint32_t sample_rate = 32000, uint32_t channels = 2);
void audio_shutdown();
void audio_update();

// Voice management
int  audio_play(const int16_t* samples, uint32_t count, float volume = 1.0f, float pan = 0.0f);
void audio_stop(int voice_id);
void audio_set_volume(int voice_id, float volume);
void audio_set_pan(int voice_id, float pan);
void audio_set_pitch(int voice_id, float pitch);

} // namespace ww::audio
