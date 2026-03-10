#pragma once
// =============================================================================
// Wind Waker Static Recompilation - GX Graphics API
// Implements the GameCube's GX graphics API on top of Direct3D 11
// The TEV (Texture Environment) unit is the heart of GameCube rendering:
//   up to 16 stages, each computing: result = D + ((1-C)*A + C*B)
// =============================================================================

#include <cstdint>
#include <string>

namespace ww::gx {

// ---- GX Enums (matching Nintendo SDK values) --------------------------------

enum GXAttr : uint32_t {
    GX_VA_PNMTXIDX  = 0,
    GX_VA_TEX0MTXIDX = 1,
    GX_VA_POS       = 9,
    GX_VA_NRM       = 10,
    GX_VA_CLR0      = 11,
    GX_VA_CLR1      = 12,
    GX_VA_TEX0      = 13,
    GX_VA_TEX1      = 14,
    GX_VA_TEX2      = 15,
    GX_VA_TEX3      = 16,
    GX_VA_TEX4      = 17,
    GX_VA_TEX5      = 18,
    GX_VA_TEX6      = 19,
    GX_VA_TEX7      = 20,
    GX_VA_MAX_ATTR  = 21,
    GX_VA_NULL      = 0xFF,
};

enum GXAttrType : uint32_t {
    GX_NONE    = 0,
    GX_DIRECT  = 1,
    GX_INDEX8  = 2,
    GX_INDEX16 = 3,
};

enum GXPrimitive : uint32_t {
    GX_QUADS         = 0x80,
    GX_TRIANGLES     = 0x90,
    GX_TRIANGLESTRIP = 0x98,
    GX_TRIANGLEFAN   = 0xA0,
    GX_LINES         = 0xA8,
    GX_LINESTRIP     = 0xB0,
    GX_POINTS        = 0xB8,
};

enum GXTexFmt : uint32_t {
    GX_TF_I4     = 0x0,
    GX_TF_I8     = 0x1,
    GX_TF_IA4    = 0x2,
    GX_TF_IA8    = 0x3,
    GX_TF_RGB565 = 0x4,
    GX_TF_RGB5A3 = 0x5,
    GX_TF_RGBA8  = 0x6,
    GX_TF_C4     = 0x8,  // 4-bit paletted
    GX_TF_C8     = 0x9,  // 8-bit paletted
    GX_TF_C14X2  = 0xA,  // 14-bit paletted
    GX_TF_CMPR   = 0xE,  // S3TC/DXT1 variant
};

enum GXTevStageID : uint32_t {
    GX_TEVSTAGE0  = 0,
    GX_TEVSTAGE1  = 1,
    GX_TEVSTAGE2  = 2,
    GX_TEVSTAGE3  = 3,
    GX_TEVSTAGE4  = 4,
    GX_TEVSTAGE5  = 5,
    GX_TEVSTAGE6  = 6,
    GX_TEVSTAGE7  = 7,
    GX_TEVSTAGE8  = 8,
    GX_TEVSTAGE9  = 9,
    GX_TEVSTAGE10 = 10,
    GX_TEVSTAGE11 = 11,
    GX_TEVSTAGE12 = 12,
    GX_TEVSTAGE13 = 13,
    GX_TEVSTAGE14 = 14,
    GX_TEVSTAGE15 = 15,
    GX_MAX_TEVSTAGE = 16,
};

// TEV combiner inputs
enum GXTevColorArg : uint32_t {
    GX_CC_CPREV = 0,  GX_CC_APREV = 1,
    GX_CC_C0    = 2,  GX_CC_A0    = 3,
    GX_CC_C1    = 4,  GX_CC_A1    = 5,
    GX_CC_C2    = 6,  GX_CC_A2    = 7,
    GX_CC_TEXC  = 8,  GX_CC_TEXA  = 9,
    GX_CC_RASC  = 10, GX_CC_RASA  = 11,
    GX_CC_ONE   = 12, GX_CC_HALF  = 13,
    GX_CC_KONST = 14, GX_CC_ZERO  = 15,
};

enum GXTevAlphaArg : uint32_t {
    GX_CA_APREV = 0, GX_CA_A0    = 1,
    GX_CA_A1    = 2, GX_CA_A2    = 3,
    GX_CA_TEXA  = 4, GX_CA_RASA  = 5,
    GX_CA_KONST = 6, GX_CA_ZERO  = 7,
};

enum GXTevOp : uint32_t {
    GX_TEV_ADD           = 0,
    GX_TEV_SUB           = 1,
    // Comparison ops (used when bias = 3)
    GX_TEV_COMP_R8_GT    = 8,
    GX_TEV_COMP_R8_EQ    = 9,
    GX_TEV_COMP_GR16_GT  = 10,
    GX_TEV_COMP_GR16_EQ  = 11,
    GX_TEV_COMP_BGR24_GT = 12,
    GX_TEV_COMP_BGR24_EQ = 13,
    GX_TEV_COMP_RGB8_GT  = 14,
    GX_TEV_COMP_RGB8_EQ  = 15,
    // Alpha comparison ops
    GX_TEV_COMP_A8_GT    = 14,
    GX_TEV_COMP_A8_EQ    = 15,
};

enum GXTevBias : uint32_t {
    GX_TB_ZERO     = 0,
    GX_TB_ADDHALF  = 1,
    GX_TB_SUBHALF  = 2,
};

enum GXTevScale : uint32_t {
    GX_CS_SCALE_1 = 0,
    GX_CS_SCALE_2 = 1,
    GX_CS_SCALE_4 = 2,
    GX_CS_DIVIDE_2 = 3,
};

enum GXBlendMode : uint32_t {
    GX_BM_NONE     = 0,
    GX_BM_BLEND    = 1,
    GX_BM_LOGIC    = 2,
    GX_BM_SUBTRACT = 3,
};

enum GXBlendFactor : uint32_t {
    GX_BL_ZERO     = 0,
    GX_BL_ONE      = 1,
    GX_BL_SRCCLR   = 2,
    GX_BL_INVSRCCLR = 3,
    GX_BL_SRCALPHA  = 4,
    GX_BL_INVSRCALPHA = 5,
    GX_BL_DSTALPHA  = 6,
    GX_BL_INVDSTALPHA = 7,
};

enum GXCompare : uint32_t {
    GX_NEVER   = 0,
    GX_LESS    = 1,
    GX_EQUAL   = 2,
    GX_LEQUAL  = 3,
    GX_GREATER = 4,
    GX_NEQUAL  = 5,
    GX_GEQUAL  = 6,
    GX_ALWAYS  = 7,
};

enum GXCullMode : uint32_t {
    GX_CULL_NONE  = 0,
    GX_CULL_FRONT = 1,
    GX_CULL_BACK  = 2,
    GX_CULL_ALL   = 3,
};

// ---- TEV Stage Configuration -----------------------------------------------

struct GXTevStageConfig {
    // Color combiner: result = d + ((1-c)*a + c*b) + bias, scaled
    GXTevColorArg color_a, color_b, color_c, color_d;
    GXTevOp       color_op;
    GXTevBias     color_bias;
    GXTevScale    color_scale;
    bool          color_clamp;
    uint32_t      color_reg_id;  // Output register (0=PREV, 1-3=TEV regs)

    // Alpha combiner
    GXTevAlphaArg alpha_a, alpha_b, alpha_c, alpha_d;
    GXTevOp       alpha_op;
    GXTevBias     alpha_bias;
    GXTevScale    alpha_scale;
    bool          alpha_clamp;
    uint32_t      alpha_reg_id;

    // Texture and rasterizer channel assignments
    uint32_t      tex_coord_id;
    uint32_t      tex_map_id;
    uint32_t      channel_id;
};

// ---- Indirect Texture Stage Config ------------------------------------------

struct GXIndTexStageConfig {
    uint32_t tex_coord;     // Texture coordinate source
    uint32_t tex_map;       // Texture map to sample
};

struct GXIndTevConfig {
    uint8_t  ind_stage;     // Which indirect stage (0-3) to use
    uint8_t  format;        // Indirect format (3-8 bit)
    uint8_t  bias;          // Bias selection
    uint8_t  matrix;        // Indirect matrix
    uint8_t  wrap_s;        // S wrap
    uint8_t  wrap_t;        // T wrap
    bool     add_prev;      // Add previous stage's offset
    bool     utc_lod;       // Use unmodified TC for LOD
};

// ---- GX State ---------------------------------------------------------------

struct GXState {
    // TEV configuration
    uint32_t         num_tev_stages;
    GXTevStageConfig tev_stages[GX_MAX_TEVSTAGE];

    // Indirect texture configuration (used heavily by WW for water/ocean)
    uint32_t          num_ind_stages;
    GXIndTexStageConfig ind_stages[4];
    GXIndTevConfig      ind_tev[GX_MAX_TEVSTAGE];

    // Konst color registers (4 RGBA colors available to TEV)
    float            konst[4][4];  // [reg][RGBA]

    // TEV color registers (PREV, REG0, REG1, REG2)
    float            tev_reg[4][4]; // [reg][RGBA]

    // Konst color/alpha selection per TEV stage
    uint8_t          kcolor_sel[GX_MAX_TEVSTAGE];
    uint8_t          kalpha_sel[GX_MAX_TEVSTAGE];

    // Vertex format
    GXAttrType       vtx_attr_type[GX_VA_MAX_ATTR];

    // Blending
    GXBlendMode      blend_mode;
    GXBlendFactor    src_factor;
    GXBlendFactor    dst_factor;

    // Depth
    bool             z_enable;
    bool             z_write;
    GXCompare        z_func;

    // Alpha compare
    GXCompare        alpha_func0;
    GXCompare        alpha_func1;
    uint8_t          alpha_op;        // AND/OR/XOR/XNOR
    uint8_t          alpha_ref0;
    uint8_t          alpha_ref1;

    // Culling
    GXCullMode       cull_mode;

    // Fog
    uint8_t          fog_type;
    float            fog_start, fog_end;
    float            fog_near, fog_far;
    float            fog_color[4];

    // Viewport
    float            viewport_x, viewport_y;
    float            viewport_w, viewport_h;
    float            viewport_near, viewport_far;

    // Scissor
    uint32_t         scissor_x, scissor_y;
    uint32_t         scissor_w, scissor_h;

    // Number of texture generators
    uint32_t         num_tex_gens;

    // Number of color channels
    uint32_t         num_color_chans;

    void reset();
};

// ---- GX API Functions (called by recompiled game code) ----------------------
// These mirror the Nintendo GX SDK functions

void GXInit();
void GXSetViewport(float x, float y, float w, float h, float near, float far);
void GXSetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void GXSetCullMode(GXCullMode mode);
void GXSetBlendMode(GXBlendMode type, GXBlendFactor src, GXBlendFactor dst, uint32_t logic_op);
void GXSetZMode(bool enable, GXCompare func, bool write_enable);
void GXSetNumTevStages(uint32_t count);
void GXSetTevOrder(GXTevStageID stage, uint32_t coord, uint32_t map, uint32_t color);
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d);
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp, uint32_t reg);
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d);
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, bool clamp, uint32_t reg);
void GXBegin(GXPrimitive prim, uint32_t vtx_fmt, uint32_t num_verts);
void GXEnd();
void GXSetVtxAttrFmt(uint32_t fmt, GXAttr attr, uint32_t comp_type, uint32_t comp_size, uint32_t frac);
void GXClearVtxDesc();
void GXSetVtxDesc(GXAttr attr, GXAttrType type);
void GXCopyDisp(void* dest, bool clear);
void GXSetCopyClear(uint32_t color, uint32_t z);
void GXDrawDone();
void GXFlush();

// Konst color registers
void GXSetTevKColor(uint32_t reg, uint32_t color);
void GXSetTevKColorSel(GXTevStageID stage, uint8_t sel);
void GXSetTevKAlphaSel(GXTevStageID stage, uint8_t sel);

// TEV color registers
void GXSetTevColor(uint32_t reg, uint32_t color);

// Indirect texture
void GXSetNumIndStages(uint32_t count);
void GXSetIndTexOrder(uint32_t stage, uint32_t coord, uint32_t map);
void GXSetTevIndirect(GXTevStageID stage, uint32_t ind_stage, uint32_t fmt,
                       uint32_t bias, uint32_t matrix, uint32_t wrap_s,
                       uint32_t wrap_t, bool add_prev, bool utc_lod);

// Alpha compare
void GXSetAlphaCompare(GXCompare func0, uint8_t ref0, uint8_t op, GXCompare func1, uint8_t ref1);

// Fog
void GXSetFog(uint8_t type, float start, float end, float near_z, float far_z, uint32_t color);

// Texture coordinate generation
void GXSetNumTexGens(uint32_t count);

// Color channels
void GXSetNumChans(uint32_t count);

// Matrix operations
void GXLoadPosMtxImm(const float mtx[3][4], uint32_t id);
void GXSetCurrentMtx(uint32_t id);
void GXSetProjection(const float mtx[4][4], uint32_t type);

// Display lists
void GXCallDisplayList(const uint8_t* data, uint32_t size);

// TEV shader generation
std::string generate_tev_shader(const GXState& state);

// Backend initialization (D3D11)
bool GXInitBackend(void* hwnd, uint32_t width, uint32_t height);
void GXShutdownBackend();
void GXPresent();

} // namespace ww::gx
