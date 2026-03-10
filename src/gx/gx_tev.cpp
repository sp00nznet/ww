// =============================================================================
// GX TEV (Texture Environment) Stage Emulation
// The TEV is the GameCube's pixel shader. Each of up to 16 stages computes:
//   color_result = D op ((1-C)*A + C*B) + bias, scaled and clamped
//   alpha_result = D op ((1-C)*A + C*B) + bias, scaled and clamped
//
// We generate HLSL shaders on-the-fly based on the current TEV configuration.
// This clean-room implementation uses hardware documentation and CC0 references
// (GameCubeRecompiled, Pureikyubu) for correctness.
//
// Konst color selection, alpha compare, fog, and indirect texture support
// are critical for Wind Waker's rendering (ocean, sky, cel shading).
// =============================================================================

#include "ww/gx/gx.h"
#include "ww/hw/gc_hw.h"
#include <cstdio>
#include <string>
#include <sstream>
#include <unordered_map>

namespace ww::gx {

// Cache compiled shader programs by their TEV configuration hash
static std::unordered_map<uint64_t, void*> shader_cache;

// ---- Helper: generate HLSL for a konst color selection ----
static std::string konst_color_sel(uint8_t sel, int stage) {
    // See hw::KonstColorSel
    switch (sel) {
    case 0x00: return "float3(1.0, 1.0, 1.0)";        // 1
    case 0x01: return "float3(0.875, 0.875, 0.875)";   // 7/8
    case 0x02: return "float3(0.75, 0.75, 0.75)";      // 3/4
    case 0x03: return "float3(0.625, 0.625, 0.625)";   // 5/8
    case 0x04: return "float3(0.5, 0.5, 0.5)";         // 1/2
    case 0x05: return "float3(0.375, 0.375, 0.375)";   // 3/8
    case 0x06: return "float3(0.25, 0.25, 0.25)";      // 1/4
    case 0x07: return "float3(0.125, 0.125, 0.125)";   // 1/8
    case 0x0C: return "konst[0].rgb";
    case 0x0D: return "konst[1].rgb";
    case 0x0E: return "konst[2].rgb";
    case 0x0F: return "konst[3].rgb";
    case 0x10: return "konst[0].rrr";
    case 0x11: return "konst[1].rrr";
    case 0x12: return "konst[2].rrr";
    case 0x13: return "konst[3].rrr";
    case 0x14: return "konst[0].ggg";
    case 0x15: return "konst[1].ggg";
    case 0x16: return "konst[2].ggg";
    case 0x17: return "konst[3].ggg";
    case 0x18: return "konst[0].bbb";
    case 0x19: return "konst[1].bbb";
    case 0x1A: return "konst[2].bbb";
    case 0x1B: return "konst[3].bbb";
    case 0x1C: return "konst[0].aaa";
    case 0x1D: return "konst[1].aaa";
    case 0x1E: return "konst[2].aaa";
    case 0x1F: return "konst[3].aaa";
    default:   return "float3(1.0, 1.0, 1.0)";
    }
}

static std::string konst_alpha_sel(uint8_t sel, int stage) {
    switch (sel) {
    case 0x00: return "1.0";
    case 0x01: return "0.875";
    case 0x02: return "0.75";
    case 0x03: return "0.625";
    case 0x04: return "0.5";
    case 0x05: return "0.375";
    case 0x06: return "0.25";
    case 0x07: return "0.125";
    case 0x10: return "konst[0].r";
    case 0x11: return "konst[1].r";
    case 0x12: return "konst[2].r";
    case 0x13: return "konst[3].r";
    case 0x14: return "konst[0].g";
    case 0x15: return "konst[1].g";
    case 0x16: return "konst[2].g";
    case 0x17: return "konst[3].g";
    case 0x18: return "konst[0].b";
    case 0x19: return "konst[1].b";
    case 0x1A: return "konst[2].b";
    case 0x1B: return "konst[3].b";
    case 0x1C: return "konst[0].a";
    case 0x1D: return "konst[1].a";
    case 0x1E: return "konst[2].a";
    case 0x1F: return "konst[3].a";
    default:   return "1.0";
    }
}

// ---- Helper: generate HLSL for alpha compare function ----
static std::string alpha_compare_expr(GXCompare func, const char* ref_name) {
    switch (func) {
    case GX_NEVER:   return "false";
    case GX_LESS:    return std::string("prev.a < ") + ref_name;
    case GX_EQUAL:   return std::string("abs(prev.a - ") + ref_name + ") < 0.004";
    case GX_LEQUAL:  return std::string("prev.a <= ") + ref_name;
    case GX_GREATER: return std::string("prev.a > ") + ref_name;
    case GX_NEQUAL:  return std::string("abs(prev.a - ") + ref_name + ") >= 0.004";
    case GX_GEQUAL:  return std::string("prev.a >= ") + ref_name;
    case GX_ALWAYS:  return "true";
    default:         return "true";
    }
}

// ---- Helper: generate HLSL for bias ----
static std::string bias_str(GXTevBias bias) {
    switch (bias) {
    case GX_TB_ZERO:    return "";
    case GX_TB_ADDHALF: return " + 0.5";
    case GX_TB_SUBHALF: return " - 0.5";
    default:            return "";
    }
}

// ---- Helper: generate HLSL for scale ----
static std::string scale_prefix(GXTevScale scale) {
    switch (scale) {
    case GX_CS_SCALE_1:  return "";
    case GX_CS_SCALE_2:  return "2.0 * ";
    case GX_CS_SCALE_4:  return "4.0 * ";
    case GX_CS_DIVIDE_2: return "0.5 * ";
    default:             return "";
    }
}

// ---- Helper: output register name ----
static std::string reg_name(uint32_t reg_id) {
    switch (reg_id) {
    case 0: return "prev";
    case 1: return "c0";
    case 2: return "c1";
    case 3: return "c2";
    default: return "prev";
    }
}

// Generate HLSL source for the current TEV configuration
std::string generate_tev_shader(const GXState& state) {
    std::stringstream ss;

    ss << "// Auto-generated TEV shader (" << state.num_tev_stages << " stages";
    if (state.num_ind_stages > 0) ss << ", " << state.num_ind_stages << " indirect";
    ss << ")\n\n";

    // Input struct
    ss << "struct PSInput {\n";
    ss << "    float4 position : SV_POSITION;\n";
    ss << "    float4 color0 : COLOR0;\n";
    ss << "    float4 color1 : COLOR1;\n";
    for (uint32_t i = 0; i < 8; i++) {
        ss << "    float2 texcoord" << i << " : TEXCOORD" << i << ";\n";
    }
    ss << "};\n\n";

    // Textures and samplers
    ss << "Texture2D tex0 : register(t0);\n";
    ss << "Texture2D tex1 : register(t1);\n";
    ss << "Texture2D tex2 : register(t2);\n";
    ss << "Texture2D tex3 : register(t3);\n";
    ss << "Texture2D tex4 : register(t4);\n";
    ss << "Texture2D tex5 : register(t5);\n";
    ss << "Texture2D tex6 : register(t6);\n";
    ss << "Texture2D tex7 : register(t7);\n";
    ss << "SamplerState samp0 : register(s0);\n";
    ss << "SamplerState samp1 : register(s1);\n";
    ss << "SamplerState samp2 : register(s2);\n";
    ss << "SamplerState samp3 : register(s3);\n";
    ss << "SamplerState samp4 : register(s4);\n";
    ss << "SamplerState samp5 : register(s5);\n";
    ss << "SamplerState samp6 : register(s6);\n";
    ss << "SamplerState samp7 : register(s7);\n\n";

    // Constant buffer
    ss << "cbuffer TevConstants : register(b0) {\n";
    ss << "    float4 konst[4];\n";
    ss << "    float4 tev_color[4];\n";
    ss << "    float4 fog_params;\n";    // x=start, y=end, z=1/(end-start), w=type
    ss << "    float4 fog_color;\n";
    ss << "    float4 alpha_ref;\n";     // x=ref0/255, y=ref1/255
    ss << "};\n\n";

    // Helper to sample texture by index
    ss << "float4 SampleTex(uint idx, float2 uv) {\n";
    ss << "    switch (idx) {\n";
    for (int i = 0; i < 8; i++) {
        ss << "        case " << i << ": return tex" << i << ".Sample(samp" << i << ", uv);\n";
    }
    ss << "        default: return float4(1,1,1,1);\n";
    ss << "    }\n";
    ss << "}\n\n";

    // Main pixel shader
    ss << "float4 main(PSInput input) : SV_TARGET {\n";
    ss << "    float4 prev = tev_color[0];\n";  // PREV register
    ss << "    float4 c0 = tev_color[1];\n";
    ss << "    float4 c1 = tev_color[2];\n";
    ss << "    float4 c2 = tev_color[3];\n";
    ss << "    float4 tex_color = float4(1,1,1,1);\n";
    ss << "    float4 ras_color = float4(1,1,1,1);\n";
    ss << "    float2 texcoords[8] = {\n";
    for (int i = 0; i < 8; i++) {
        ss << "        input.texcoord" << i;
        if (i < 7) ss << ",";
        ss << "\n";
    }
    ss << "    };\n";

    // Indirect texture offsets (if any indirect stages)
    if (state.num_ind_stages > 0) {
        ss << "    float3 ind_offset = float3(0,0,0);\n";
    }
    ss << "\n";

    // TEV stages
    for (uint32_t i = 0; i < state.num_tev_stages; i++) {
        const auto& stage = state.tev_stages[i];
        const auto& ind = state.ind_tev[i];

        ss << "    // === TEV Stage " << i << " ===\n";

        // Determine texture coordinate (may be offset by indirect)
        std::string tc;
        if (stage.tex_coord_id < 8) {
            tc = "texcoords[" + std::to_string(stage.tex_coord_id) + "]";
        } else {
            tc = "float2(0,0)";
        }

        // Indirect texture offset
        if (state.num_ind_stages > 0 && ind.ind_stage < 4 &&
            (ind.matrix != 0 || ind.add_prev)) {
            uint32_t ind_map = state.ind_stages[ind.ind_stage].tex_map;
            uint32_t ind_coord = state.ind_stages[ind.ind_stage].tex_coord;
            std::string ind_tc = (ind_coord < 8) ?
                "texcoords[" + std::to_string(ind_coord) + "]" : "float2(0,0)";

            ss << "    {\n";
            ss << "        float4 ind_sample = SampleTex(" << ind_map << ", " << ind_tc << ");\n";
            ss << "        float2 ind_off = (ind_sample.rg * 255.0 - 128.0) / 128.0;\n";
            if (ind.add_prev && i > 0) {
                ss << "        ind_off += ind_offset.xy;\n";
            }
            ss << "        ind_offset = float3(ind_off, 0);\n";
            // Apply to texture coordinate
            ss << "        " << tc << " += ind_off * 0.1;\n";
            ss << "    }\n";
        }

        // Texture fetch
        if (stage.tex_map_id < 8) {
            ss << "    tex_color = SampleTex(" << stage.tex_map_id << ", " << tc << ");\n";
        }

        // Rasterizer color
        if (stage.channel_id == 0) {
            ss << "    ras_color = input.color0;\n";
        } else if (stage.channel_id == 1) {
            ss << "    ras_color = input.color1;\n";
        }
        // channel_id == 0xFF means no rasterized color (use zero)

        // Get per-stage konst selection
        std::string kc = konst_color_sel(state.kcolor_sel[i], i);
        std::string ka = konst_alpha_sel(state.kalpha_sel[i], i);

        // ---- Color combiner ----
        auto color_arg = [&](GXTevColorArg arg) -> std::string {
            switch (arg) {
            case GX_CC_CPREV: return "prev.rgb";
            case GX_CC_APREV: return "prev.aaa";
            case GX_CC_C0:    return "c0.rgb";
            case GX_CC_A0:    return "c0.aaa";
            case GX_CC_C1:    return "c1.rgb";
            case GX_CC_A1:    return "c1.aaa";
            case GX_CC_C2:    return "c2.rgb";
            case GX_CC_A2:    return "c2.aaa";
            case GX_CC_TEXC:  return "tex_color.rgb";
            case GX_CC_TEXA:  return "tex_color.aaa";
            case GX_CC_RASC:  return "ras_color.rgb";
            case GX_CC_RASA:  return "ras_color.aaa";
            case GX_CC_ONE:   return "float3(1,1,1)";
            case GX_CC_HALF:  return "float3(0.5,0.5,0.5)";
            case GX_CC_KONST: return kc;
            case GX_CC_ZERO:  return "float3(0,0,0)";
            default:          return "float3(0,0,0)";
            }
        };

        std::string a = color_arg(stage.color_a);
        std::string b = color_arg(stage.color_b);
        std::string c = color_arg(stage.color_c);
        std::string d = color_arg(stage.color_d);

        std::string dest_c = reg_name(stage.color_reg_id);
        std::string sc = scale_prefix(stage.color_scale);
        std::string bi = bias_str(stage.color_bias);

        // TEV formula: dest = D op ((1-C)*A + C*B + bias) * scale
        // Using lerp: (1-C)*A + C*B = lerp(A, B, C)
        if (stage.color_op == GX_TEV_ADD) {
            ss << "    " << dest_c << ".rgb = " << sc << "(" << d
               << " + lerp(" << a << ", " << b << ", " << c << ")" << bi << ");\n";
        } else {
            ss << "    " << dest_c << ".rgb = " << sc << "(" << d
               << " - lerp(" << a << ", " << b << ", " << c << ")" << bi << ");\n";
        }

        if (stage.color_clamp) {
            ss << "    " << dest_c << ".rgb = saturate(" << dest_c << ".rgb);\n";
        }

        // ---- Alpha combiner ----
        auto alpha_arg = [&](GXTevAlphaArg arg) -> std::string {
            switch (arg) {
            case GX_CA_APREV: return "prev.a";
            case GX_CA_A0:    return "c0.a";
            case GX_CA_A1:    return "c1.a";
            case GX_CA_A2:    return "c2.a";
            case GX_CA_TEXA:  return "tex_color.a";
            case GX_CA_RASA:  return "ras_color.a";
            case GX_CA_KONST: return ka;
            case GX_CA_ZERO:  return "0.0";
            default:          return "0.0";
            }
        };

        std::string aa = alpha_arg(stage.alpha_a);
        std::string ab = alpha_arg(stage.alpha_b);
        std::string ac = alpha_arg(stage.alpha_c);
        std::string ad = alpha_arg(stage.alpha_d);

        std::string dest_a = reg_name(stage.alpha_reg_id);
        std::string sa = scale_prefix(stage.alpha_scale);
        std::string bia = bias_str(stage.alpha_bias);

        if (stage.alpha_op == GX_TEV_ADD) {
            ss << "    " << dest_a << ".a = " << sa << "(" << ad
               << " + lerp(" << aa << ", " << ab << ", " << ac << ")" << bia << ");\n";
        } else {
            ss << "    " << dest_a << ".a = " << sa << "(" << ad
               << " - lerp(" << aa << ", " << ab << ", " << ac << ")" << bia << ");\n";
        }

        if (stage.alpha_clamp) {
            ss << "    " << dest_a << ".a = saturate(" << dest_a << ".a);\n";
        }

        ss << "\n";
    }

    // ---- Alpha Compare (discard) ----
    bool need_alpha_test =
        (state.alpha_func0 != GX_ALWAYS || state.alpha_func1 != GX_ALWAYS);
    if (need_alpha_test) {
        std::string cmp0 = alpha_compare_expr(state.alpha_func0, "alpha_ref.x");
        std::string cmp1 = alpha_compare_expr(state.alpha_func1, "alpha_ref.y");

        std::string combine;
        switch (state.alpha_op) {
        case 0: combine = "(" + cmp0 + ") && (" + cmp1 + ")"; break; // AND
        case 1: combine = "(" + cmp0 + ") || (" + cmp1 + ")"; break; // OR
        case 2: combine = "((" + cmp0 + ") != (" + cmp1 + "))"; break; // XOR
        case 3: combine = "((" + cmp0 + ") == (" + cmp1 + "))"; break; // XNOR
        default: combine = "true"; break;
        }

        ss << "    // Alpha test\n";
        ss << "    if (!(" << combine << ")) discard;\n\n";
    }

    // ---- Fog ----
    if (state.fog_type != 0) {
        ss << "    // Fog\n";
        ss << "    {\n";
        ss << "        float fog_depth = input.position.z / input.position.w;\n";
        if (state.fog_type == 2) {
            // Linear fog
            ss << "        float fog_factor = saturate((fog_params.y - fog_depth) * fog_params.z);\n";
        } else {
            // Exponential — simplified
            ss << "        float fog_factor = saturate(exp(-fog_depth * fog_params.z));\n";
        }
        ss << "        prev.rgb = lerp(fog_color.rgb, prev.rgb, fog_factor);\n";
        ss << "    }\n\n";
    }

    ss << "    return prev;\n";
    ss << "}\n";

    return ss.str();
}

// Compute a hash of the current TEV state for shader caching
uint64_t hash_tev_state(const GXState& state) {
    uint64_t hash = 0x517CC1B727220A95ULL; // FNV offset basis
    const uint64_t prime = 0x00000100000001B3ULL;

    auto feed = [&](uint64_t v) {
        hash ^= v;
        hash *= prime;
    };

    feed(state.num_tev_stages);
    feed(state.num_ind_stages);

    for (uint32_t i = 0; i < state.num_tev_stages; i++) {
        const auto& s = state.tev_stages[i];
        feed(s.color_a | (s.color_b << 4) | (s.color_c << 8) | (s.color_d << 12));
        feed(s.color_op | (s.color_bias << 4) | (s.color_scale << 8) |
             (s.color_clamp << 12) | (s.color_reg_id << 16));
        feed(s.alpha_a | (s.alpha_b << 4) | (s.alpha_c << 8) | (s.alpha_d << 12));
        feed(s.alpha_op | (s.alpha_bias << 4) | (s.alpha_scale << 8) |
             (s.alpha_clamp << 12) | (s.alpha_reg_id << 16));
        feed(s.tex_coord_id | (s.tex_map_id << 8) | (s.channel_id << 16));
        feed(state.kcolor_sel[i] | (state.kalpha_sel[i] << 8));

        const auto& ind = state.ind_tev[i];
        feed(ind.ind_stage | (ind.format << 4) | (ind.bias << 8) |
             (ind.matrix << 12) | (ind.wrap_s << 16) | (ind.wrap_t << 20) |
             (ind.add_prev << 24) | (ind.utc_lod << 25));
    }

    feed(state.alpha_func0 | (state.alpha_func1 << 4) | (state.alpha_op << 8));
    feed(state.fog_type);

    return hash;
}

} // namespace ww::gx
