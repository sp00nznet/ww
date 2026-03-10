// =============================================================================
// DSP ADPCM Decoder
// GameCube audio uses a custom DSP ADPCM format with 4-bit samples
// and 16 prediction coefficients. Compact and good quality for its era.
// =============================================================================

#include "ww/audio/audio.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ww::audio {

std::vector<int16_t> decode_dsp_adpcm(const uint8_t* data, uint32_t num_samples,
                                       const DSPADPCMInfo& info, bool loop) {
    std::vector<int16_t> pcm(num_samples);

    int16_t yn1 = info.yn1;
    int16_t yn2 = info.yn2;
    uint16_t pred_scale = info.pred_scale;

    uint32_t sample_idx = 0;
    uint32_t byte_idx = 0;

    while (sample_idx < num_samples) {
        // Every 14 samples, read a new predictor/scale byte
        if (sample_idx % 14 == 0) {
            pred_scale = data[byte_idx++];
        }

        int pred = (pred_scale >> 4) & 0xF;
        int scale = 1 << (pred_scale & 0xF);
        int16_t coef1 = info.coef[pred * 2];
        int16_t coef2 = info.coef[pred * 2 + 1];

        // Process 14 nibbles (7 bytes)
        for (int n = 0; n < 14 && sample_idx < num_samples; n++) {
            int nibble;
            if (n % 2 == 0) {
                nibble = (data[byte_idx] >> 4) & 0xF;
            } else {
                nibble = data[byte_idx] & 0xF;
                byte_idx++;
            }

            // Sign extend 4-bit nibble
            if (nibble >= 8) nibble -= 16;

            // Predict and scale
            int32_t sample = (scale * nibble) +
                             ((coef1 * (int32_t)yn1 + coef2 * (int32_t)yn2 + 1024) >> 11);

            // Clamp to 16-bit
            sample = std::clamp(sample, -32768, 32767);

            pcm[sample_idx++] = (int16_t)sample;
            yn2 = yn1;
            yn1 = (int16_t)sample;
        }
    }

    return pcm;
}

} // namespace ww::audio
