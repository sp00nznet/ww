// =============================================================================
// GX Matrix Operations
// The GameCube has a 64-entry matrix memory for vertex transforms.
// Wind Waker uses these extensively for character animation, camera, etc.
// =============================================================================

#include "ww/gx/gx.h"
#include <cstring>
#include <cmath>

namespace ww::gx {

// GX matrix memory: 64 entries of 4x3 matrices (256 floats)
static float g_matrix_mem[256];

// Position matrix (model-view)
static float g_pos_matrix[4][4];

// Projection matrix
static float g_proj_matrix[4][4];

void GXLoadPosMtxImm(const float mtx[3][4], uint32_t id) {
    memcpy(&g_matrix_mem[id * 4], mtx, 12 * sizeof(float));
}

void GXSetCurrentMtx(uint32_t id) {
    // Set which matrix in memory is the current position matrix
    float* src = &g_matrix_mem[id * 4];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            g_pos_matrix[r][c] = src[r * 4 + c];
        }
    }
    g_pos_matrix[3][0] = 0; g_pos_matrix[3][1] = 0;
    g_pos_matrix[3][2] = 0; g_pos_matrix[3][3] = 1;
}

void GXSetProjection(const float mtx[4][4], uint32_t type) {
    memcpy(g_proj_matrix, mtx, 16 * sizeof(float));
}

} // namespace ww::gx
