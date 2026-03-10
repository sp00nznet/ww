// =============================================================================
// GX Core - Main GX state machine
// Tracks all GX state and dispatches to the D3D11 backend
// =============================================================================

#include "ww/gx/gx.h"
#include <cstdio>
#include <cstring>

namespace ww::gx {

static GXState g_state;

void GXState::reset() {
    memset(this, 0, sizeof(*this));
    z_enable = true;
    z_write = true;
    z_func = GX_LEQUAL;
    cull_mode = GX_CULL_BACK;
    blend_mode = GX_BM_NONE;
    src_factor = GX_BL_ONE;
    dst_factor = GX_BL_ZERO;
    viewport_w = 640;
    viewport_h = 480;
    viewport_near = 0.0f;
    viewport_far = 1.0f;
    num_tev_stages = 1;
    num_tex_gens = 0;
    num_color_chans = 0;
    num_ind_stages = 0;
    alpha_func0 = GX_ALWAYS;
    alpha_func1 = GX_ALWAYS;
    alpha_op = 0;
    alpha_ref0 = 0;
    alpha_ref1 = 0;
    fog_type = 0; // FOG_NONE

    // Default konst colors to white
    for (int i = 0; i < 4; i++) {
        konst[i][0] = konst[i][1] = konst[i][2] = konst[i][3] = 1.0f;
    }
}

void GXInit() {
    printf("[GX] Initializing GameCube graphics subsystem\n");
    g_state.reset();
}

void GXSetViewport(float x, float y, float w, float h, float near_z, float far_z) {
    g_state.viewport_x = x;
    g_state.viewport_y = y;
    g_state.viewport_w = w;
    g_state.viewport_h = h;
    g_state.viewport_near = near_z;
    g_state.viewport_far = far_z;
}

void GXSetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    g_state.scissor_x = x;
    g_state.scissor_y = y;
    g_state.scissor_w = w;
    g_state.scissor_h = h;
}

void GXSetCullMode(GXCullMode mode) {
    g_state.cull_mode = mode;
}

void GXSetBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, uint32_t logic_op) {
    g_state.blend_mode = type;
    g_state.src_factor = src;
    g_state.dst_factor = dst;
}

void GXSetZMode(bool enable, GXCompare func, bool write_enable) {
    g_state.z_enable = enable;
    g_state.z_func = func;
    g_state.z_write = write_enable;
}

void GXSetNumTevStages(uint32_t count) {
    g_state.num_tev_stages = count;
}

void GXSetTevOrder(GXTevStageID stage, uint32_t coord, uint32_t map, uint32_t color) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.tev_stages[stage].tex_coord_id = coord;
    g_state.tev_stages[stage].tex_map_id = map;
    g_state.tev_stages[stage].channel_id = color;
}

void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.tev_stages[stage].color_a = a;
    g_state.tev_stages[stage].color_b = b;
    g_state.tev_stages[stage].color_c = c;
    g_state.tev_stages[stage].color_d = d;
}

void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp, uint32_t reg) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.tev_stages[stage].color_op = op;
    g_state.tev_stages[stage].color_bias = bias;
    g_state.tev_stages[stage].color_scale = scale;
    g_state.tev_stages[stage].color_clamp = clamp;
    g_state.tev_stages[stage].color_reg_id = reg;
}

void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.tev_stages[stage].alpha_a = a;
    g_state.tev_stages[stage].alpha_b = b;
    g_state.tev_stages[stage].alpha_c = c;
    g_state.tev_stages[stage].alpha_d = d;
}

void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp, uint32_t reg) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.tev_stages[stage].alpha_op = op;
    g_state.tev_stages[stage].alpha_bias = bias;
    g_state.tev_stages[stage].alpha_scale = scale;
    g_state.tev_stages[stage].alpha_clamp = clamp;
    g_state.tev_stages[stage].alpha_reg_id = reg;
}

void GXBegin(GXPrimitive prim, uint32_t vtx_fmt, uint32_t num_verts) {
    // Begin primitive assembly
    // TODO: forward to D3D11 backend
}

void GXEnd() {
    // End primitive assembly and flush to GPU
    // TODO: forward to D3D11 backend
}

void GXSetVtxAttrFmt(uint32_t fmt, GXAttr attr, uint32_t comp_type, uint32_t comp_size, uint32_t frac) {
    // TODO: track vertex attribute formats
}

void GXClearVtxDesc() {
    for (int i = 0; i < GX_VA_MAX_ATTR; i++) {
        g_state.vtx_attr_type[i] = GX_NONE;
    }
}

void GXSetVtxDesc(GXAttr attr, GXAttrType type) {
    if (attr < GX_VA_MAX_ATTR) {
        g_state.vtx_attr_type[attr] = type;
    }
}

void GXCopyDisp(void* dest, bool clear) {
    // Copy framebuffer to XFB
    // TODO: forward to D3D11 present
}

void GXSetCopyClear(uint32_t color, uint32_t z) {
    // Set clear color/depth for GXCopyDisp
}

void GXDrawDone() {
    // Wait for GPU to finish (no-op for us, we're synchronous)
}

void GXFlush() {
    // Flush command buffer (no-op, we execute immediately)
}

// ---- Konst Color Registers ----

void GXSetTevKColor(uint32_t reg, uint32_t color) {
    if (reg >= 4) return;
    g_state.konst[reg][0] = ((color >> 24) & 0xFF) / 255.0f; // R
    g_state.konst[reg][1] = ((color >> 16) & 0xFF) / 255.0f; // G
    g_state.konst[reg][2] = ((color >> 8)  & 0xFF) / 255.0f; // B
    g_state.konst[reg][3] = ((color >> 0)  & 0xFF) / 255.0f; // A
}

void GXSetTevKColorSel(GXTevStageID stage, uint8_t sel) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.kcolor_sel[stage] = sel;
}

void GXSetTevKAlphaSel(GXTevStageID stage, uint8_t sel) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.kalpha_sel[stage] = sel;
}

// ---- TEV Color Registers ----

void GXSetTevColor(uint32_t reg, uint32_t color) {
    if (reg >= 4) return;
    g_state.tev_reg[reg][0] = ((color >> 24) & 0xFF) / 255.0f;
    g_state.tev_reg[reg][1] = ((color >> 16) & 0xFF) / 255.0f;
    g_state.tev_reg[reg][2] = ((color >> 8)  & 0xFF) / 255.0f;
    g_state.tev_reg[reg][3] = ((color >> 0)  & 0xFF) / 255.0f;
}

// ---- Indirect Texture ----

void GXSetNumIndStages(uint32_t count) {
    g_state.num_ind_stages = count;
}

void GXSetIndTexOrder(uint32_t stage, uint32_t coord, uint32_t map) {
    if (stage >= 4) return;
    g_state.ind_stages[stage].tex_coord = coord;
    g_state.ind_stages[stage].tex_map = map;
}

void GXSetTevIndirect(GXTevStageID stage, uint32_t ind_stage, uint32_t fmt,
                       uint32_t bias, uint32_t matrix, uint32_t wrap_s,
                       uint32_t wrap_t, bool add_prev, bool utc_lod) {
    if (stage >= GX_MAX_TEVSTAGE) return;
    g_state.ind_tev[stage].ind_stage = (uint8_t)ind_stage;
    g_state.ind_tev[stage].format    = (uint8_t)fmt;
    g_state.ind_tev[stage].bias      = (uint8_t)bias;
    g_state.ind_tev[stage].matrix    = (uint8_t)matrix;
    g_state.ind_tev[stage].wrap_s    = (uint8_t)wrap_s;
    g_state.ind_tev[stage].wrap_t    = (uint8_t)wrap_t;
    g_state.ind_tev[stage].add_prev  = add_prev;
    g_state.ind_tev[stage].utc_lod   = utc_lod;
}

// ---- Alpha Compare ----

void GXSetAlphaCompare(GXCompare func0, uint8_t ref0, uint8_t op, GXCompare func1, uint8_t ref1) {
    g_state.alpha_func0 = func0;
    g_state.alpha_ref0  = ref0;
    g_state.alpha_op    = op;
    g_state.alpha_func1 = func1;
    g_state.alpha_ref1  = ref1;
}

// ---- Fog ----

void GXSetFog(uint8_t type, float start, float end, float near_z, float far_z, uint32_t color) {
    g_state.fog_type  = type;
    g_state.fog_start = start;
    g_state.fog_end   = end;
    g_state.fog_near  = near_z;
    g_state.fog_far   = far_z;
    g_state.fog_color[0] = ((color >> 24) & 0xFF) / 255.0f;
    g_state.fog_color[1] = ((color >> 16) & 0xFF) / 255.0f;
    g_state.fog_color[2] = ((color >> 8)  & 0xFF) / 255.0f;
    g_state.fog_color[3] = ((color >> 0)  & 0xFF) / 255.0f;
}

// ---- Tex Gen / Color Channels ----

void GXSetNumTexGens(uint32_t count) {
    g_state.num_tex_gens = count;
}

void GXSetNumChans(uint32_t count) {
    g_state.num_color_chans = count;
}

} // namespace ww::gx
