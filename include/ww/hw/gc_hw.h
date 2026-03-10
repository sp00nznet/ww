#pragma once
// =============================================================================
// GameCube Hardware Constants & Register Definitions
// Derived from public hardware documentation and CC0/public domain sources
// (Pureikyubu/Dolwin - CC0 1.0 Universal)
//
// This header defines the GameCube's hardware-level constants, memory map,
// low-memory layout, and register addresses used by the Dolphin OS SDK.
// =============================================================================

#include <cstdint>

namespace ww::hw {

// =============================================================================
// Clock Speeds
// =============================================================================
constexpr uint32_t CPU_CLOCK_HZ       = 486000000;   // 486 MHz (Gekko)
constexpr uint32_t BUS_CLOCK_HZ       = 162000000;   // 162 MHz (system bus)
constexpr uint32_t TIMEBASE_FREQ_HZ   = 40500000;    // 40.5 MHz (bus/4, timebase counter)
constexpr uint32_t DSP_CLOCK_HZ       = 81000000;    // 81 MHz (DSP)

// =============================================================================
// Memory Map
// =============================================================================

// Main RAM (MEM1): 24 MB
constexpr uint32_t MEM1_BASE          = 0x80000000;  // Cached
constexpr uint32_t MEM1_SIZE          = 0x01800000;  // 24 MB
constexpr uint32_t MEM1_END           = MEM1_BASE + MEM1_SIZE;
constexpr uint32_t MEM1_UNCACHED      = 0xC0000000;  // Uncached mirror
constexpr uint32_t MEM1_PHYSICAL_MASK = 0x01FFFFFF;  // Physical address mask

// Hardware registers (directly mapped)
constexpr uint32_t HW_REG_BASE        = 0xCC000000;

// Sub-regions within HW registers
constexpr uint32_t CP_REG_BASE        = 0xCC000000;  // Command Processor
constexpr uint32_t PE_REG_BASE        = 0xCC001000;  // Pixel Engine
constexpr uint32_t VI_REG_BASE        = 0xCC002000;  // Video Interface
constexpr uint32_t PI_REG_BASE        = 0xCC003000;  // Processor Interface
constexpr uint32_t MI_REG_BASE        = 0xCC004000;  // Memory Interface
constexpr uint32_t DSP_REG_BASE       = 0xCC005000;  // DSP Interface
constexpr uint32_t DI_REG_BASE        = 0xCC006000;  // DVD Interface
constexpr uint32_t SI_REG_BASE        = 0xCC006400;  // Serial Interface
constexpr uint32_t EXI_REG_BASE       = 0xCC006800;  // External Interface
constexpr uint32_t AI_REG_BASE        = 0xCC006C00;  // Audio Interface
constexpr uint32_t GX_FIFO_BASE       = 0xCC008000;  // GX FIFO (write pipe)

// =============================================================================
// Dolphin OS Low-Memory Globals
// The OS and SDK store critical values at fixed addresses in low memory.
// These must be initialized before the game's main() is called.
// =============================================================================

// System info block (set by BS2/IPL bootloader)
constexpr uint32_t OS_BOOT_INFO       = 0x80000000;  // Start of boot info

// Game ID and DVD header (first 0x20 bytes are the disc header)
constexpr uint32_t OS_DVD_GAME_ID     = 0x80000000;  // 4 bytes: "GZLE" for Wind Waker US
constexpr uint32_t OS_DVD_COMPANY     = 0x80000004;  // 2 bytes: "01" (Nintendo)
constexpr uint32_t OS_DVD_DISC_NUM    = 0x80000006;  // 1 byte
constexpr uint32_t OS_DVD_VERSION     = 0x80000007;  // 1 byte

// Console type and memory size
constexpr uint32_t OS_CONSOLE_TYPE    = 0x8000002C;  // Console type: 1=retail, 2=devkit
constexpr uint32_t OS_ARENA_LO        = 0x80000030;  // Arena low bound
constexpr uint32_t OS_ARENA_HI        = 0x80000034;  // Arena high bound

// Clock values (set by OS at boot)
constexpr uint32_t OS_BUS_CLOCK       = 0x800000F8;  // Bus clock speed (162 MHz)
constexpr uint32_t OS_CPU_CLOCK       = 0x800000FC;  // CPU clock speed (486 MHz)

// Thread management
constexpr uint32_t OS_CURRENT_THREAD  = 0x800000E4;  // Pointer to current OSThread
constexpr uint32_t OS_THREAD_QUEUE    = 0x800000DC;  // Active thread queue

// Interrupt handling
constexpr uint32_t OS_INTERRUPT_TABLE = 0x80003040;  // Interrupt handler table

// Exception vectors
constexpr uint32_t OS_EXCEPTION_TABLE = 0x80000048;  // Exception table base

// Memory size info
constexpr uint32_t OS_PHYSICAL_MEM_SIZE = 0x80000028;  // Physical memory size (0x01800000)
constexpr uint32_t OS_SIMULATED_MEM_SIZE = 0x800000F0; // Simulated mem size

// Debug
constexpr uint32_t OS_DEBUG_MONITOR   = 0x800000EC;  // RAM end address
constexpr uint32_t OS_DEBUG_FLAG      = 0x800000E8;  // Debug flag

// Boot magic
constexpr uint32_t OS_BOOT_MAGIC      = 0x80000020;  // Magic word 0x0D15EA5E
constexpr uint32_t OS_BOOT_VERSION    = 0x80000024;  // Boot version (1)

// DVD filesystem info
constexpr uint32_t OS_BI2_ADDR        = 0x800000F4;  // bi2.bin address
constexpr uint32_t OS_FST_ADDR        = 0x80000038;  // FST (file system table) address
constexpr uint32_t OS_FST_MAX_LEN     = 0x8000003C;  // FST max length

// Production pads / misc
constexpr uint32_t OS_BOOT_TIME       = 0x800030D8;  // Boot time (u64)
constexpr uint32_t OS_PRODUCTION_PADS = 0x800030E0;  // Production pads (u16, = 6)
constexpr uint32_t OS_DEVKIT_BOOT     = 0x800030E4;  // 0xC0008000

// =============================================================================
// Console Type IDs
// =============================================================================
constexpr uint32_t OS_CONSOLE_RETAIL       = 0x00000001;
constexpr uint32_t OS_CONSOLE_DEVKIT       = 0x10000002;
constexpr uint32_t OS_CONSOLE_TDEV         = 0x10000003;
constexpr uint32_t OS_CONSOLE_RETAIL_HW2   = 0x00000003;  // Later retail revision

// =============================================================================
// TEV Hardware Register Definitions
// The TEV (Texture Environment) unit processes pixel shading via up to 16 stages.
// Each stage computes: result = D op ((1-C)*A + C*B + bias) * scale
// =============================================================================

// BP (Blitting Processor) register addresses
namespace bp {
    constexpr uint8_t GEN_MODE           = 0x00;  // Num TEV stages, num tex gens, etc.
    constexpr uint8_t IND_MTXA0          = 0x06;  // Indirect texture matrix A row 0
    constexpr uint8_t IND_MTXB0          = 0x07;
    constexpr uint8_t IND_MTXC0          = 0x08;
    constexpr uint8_t IND_CMD0           = 0x10;  // Indirect TEV stage 0 command
    constexpr uint8_t SCISSOR_TL         = 0x20;  // Scissor top-left
    constexpr uint8_t SCISSOR_BR         = 0x21;  // Scissor bottom-right
    constexpr uint8_t SU_LPSIZE          = 0x22;  // Line/point size
    constexpr uint8_t SU_SCIS0           = 0x23;  // Scissor offset 0
    constexpr uint8_t SU_SCIS1           = 0x24;  // Scissor offset 1
    constexpr uint8_t TEV_COLOR_ENV_0    = 0xC0;  // TEV stage 0 color combiner
    constexpr uint8_t TEV_ALPHA_ENV_0    = 0xC1;  // TEV stage 0 alpha combiner
    constexpr uint8_t TEV_COLOR_ENV_STRIDE = 2;   // Stride between stages
    constexpr uint8_t TEV_REGISTERL_0    = 0xE0;  // TEV color register 0 (low)
    constexpr uint8_t TEV_REGISTERH_0    = 0xE1;  // TEV color register 0 (high)
    constexpr uint8_t TEV_FOG_PARAM_0    = 0xEE;  // Fog parameters
    constexpr uint8_t TEV_FOG_PARAM_1    = 0xEF;
    constexpr uint8_t TEV_FOG_PARAM_2    = 0xF0;
    constexpr uint8_t TEV_FOG_PARAM_3    = 0xF1;
    constexpr uint8_t TEV_FOG_COLOR      = 0xF2;  // Fog color
    constexpr uint8_t ALPHA_COMPARE      = 0xF3;  // Alpha compare
    constexpr uint8_t ZMode              = 0x40;   // Z-buffer mode
    constexpr uint8_t BLEND_MODE         = 0x41;   // Blend mode
    constexpr uint8_t PE_ZMODE           = 0x43;   // Z mode
    constexpr uint8_t PE_CMODE0          = 0x41;   // Color/blend control
    constexpr uint8_t PE_CMODE1          = 0x42;   // Alpha control
    constexpr uint8_t PE_CONTROL         = 0x43;   // Pixel format, z format
    constexpr uint8_t COPY_CLEAR_AR      = 0x4F;   // EFB copy clear color (AR)
    constexpr uint8_t COPY_CLEAR_GB      = 0x50;   // EFB copy clear color (GB)
    constexpr uint8_t COPY_CLEAR_Z       = 0x51;   // EFB copy clear Z
    constexpr uint8_t TRIGGER_EFB_COPY   = 0x52;   // Trigger EFB copy
    constexpr uint8_t TEV_KSEL_0         = 0xF6;   // TEV Konst color select 0
    // TEV_KSEL_0 through TEV_KSEL_7 (0xF6-0xFD)
    constexpr uint8_t SS_MASK            = 0xFE;   // BP mask register
}

// CP (Command Processor) register addresses
namespace cp {
    constexpr uint8_t MATINDEX_A        = 0x30;  // Matrix index A
    constexpr uint8_t MATINDEX_B        = 0x40;  // Matrix index B
    constexpr uint8_t VCD_LO            = 0x50;  // Vertex descriptor (low)
    constexpr uint8_t VCD_HI            = 0x60;  // Vertex descriptor (high)
    constexpr uint8_t VAT_A             = 0x70;  // Vertex attribute table A (group 0-7)
    constexpr uint8_t VAT_B             = 0x80;  // Vertex attribute table B
    constexpr uint8_t VAT_C             = 0x90;  // Vertex attribute table C
    constexpr uint8_t ARRAY_BASE        = 0xA0;  // Vertex array base addresses (0xA0-0xAF)
    constexpr uint8_t ARRAY_STRIDE      = 0xB0;  // Vertex array strides (0xB0-0xBF)
}

// XF (Transform unit) register addresses
namespace xf {
    constexpr uint16_t XF_ERROR          = 0x1000;
    constexpr uint16_t XF_DIAGNOSTICS    = 0x1001;
    constexpr uint16_t XF_STATE0         = 0x1002;
    constexpr uint16_t XF_STATE1         = 0x1003;
    constexpr uint16_t XF_CLOCK          = 0x1004;
    constexpr uint16_t XF_CLIPDISABLE    = 0x1005;
    constexpr uint16_t XF_PERF0          = 0x1006;
    constexpr uint16_t XF_PERF1          = 0x1007;
    constexpr uint16_t XF_INVTXSPEC      = 0x1008;
    constexpr uint16_t XF_NUMCOLORS      = 0x1009;
    constexpr uint16_t XF_AMBIENT0       = 0x100A;
    constexpr uint16_t XF_AMBIENT1       = 0x100B;
    constexpr uint16_t XF_MATERIAL0      = 0x100C;
    constexpr uint16_t XF_MATERIAL1      = 0x100D;
    constexpr uint16_t XF_COLOR0CNTRL    = 0x100E;
    constexpr uint16_t XF_COLOR1CNTRL    = 0x100F;
    constexpr uint16_t XF_ALPHA0CNTRL    = 0x1010;
    constexpr uint16_t XF_ALPHA1CNTRL    = 0x1011;
    constexpr uint16_t XF_DUALTEXTRAN    = 0x1012;
    constexpr uint16_t XF_MATRIXINDEX_A  = 0x1018;
    constexpr uint16_t XF_MATRIXINDEX_B  = 0x1019;
    constexpr uint16_t XF_PROJECTION     = 0x1020;  // 7 regs (0x1020-0x1026)
    constexpr uint16_t XF_NUMTEXGENS     = 0x103F;
    constexpr uint16_t XF_TEXGEN0        = 0x1040;  // 8 regs (0x1040-0x1047)
    constexpr uint16_t XF_DUALTEX0       = 0x1050;  // 8 regs (0x1050-0x1057)
    // Position/normal matrix memory: 0x0000-0x00FF (64 entries x 4 floats)
    // Texture matrix memory: 0x0500-0x05FF
    // Light memory: 0x0600-0x067F
}

// =============================================================================
// Video Interface (VI) constants
// =============================================================================
enum VITVMode : uint32_t {
    VI_TVMODE_NTSC_INT   = 0x0000,  // NTSC interlaced (480i)
    VI_TVMODE_NTSC_DS    = 0x0001,  // NTSC double-strike
    VI_TVMODE_NTSC_PROG  = 0x0002,  // NTSC progressive (480p)
    VI_TVMODE_PAL_INT    = 0x0100,
    VI_TVMODE_PAL_DS     = 0x0101,
    VI_TVMODE_PAL_PROG   = 0x0102,
    VI_TVMODE_MPAL_INT   = 0x0200,
    VI_TVMODE_MPAL_DS    = 0x0201,
    VI_TVMODE_MPAL_PROG  = 0x0202,
};

constexpr uint32_t VI_DISPLAY_WIDTH    = 640;
constexpr uint32_t VI_DISPLAY_HEIGHT   = 480;  // NTSC effective
constexpr uint32_t VI_DISPLAY_HEIGHT_I = 528;  // Full interlaced
constexpr uint32_t EFB_WIDTH           = 640;
constexpr uint32_t EFB_HEIGHT          = 528;

// =============================================================================
// GX FIFO Command Opcodes
// =============================================================================
namespace fifo {
    constexpr uint8_t CMD_NOP            = 0x00;
    constexpr uint8_t CMD_LOAD_CP_REG    = 0x08;
    constexpr uint8_t CMD_LOAD_XF_REG    = 0x10;
    constexpr uint8_t CMD_LOAD_INDX_A    = 0x20;  // Position matrix
    constexpr uint8_t CMD_LOAD_INDX_B    = 0x28;  // Normal matrix
    constexpr uint8_t CMD_LOAD_INDX_C    = 0x30;  // Texture coord matrix
    constexpr uint8_t CMD_LOAD_INDX_D    = 0x38;  // Light
    constexpr uint8_t CMD_LOAD_BP_REG    = 0x61;
    constexpr uint8_t CMD_DRAW_QUADS     = 0x80;
    constexpr uint8_t CMD_DRAW_TRIS      = 0x90;
    constexpr uint8_t CMD_DRAW_TRISTRIP  = 0x98;
    constexpr uint8_t CMD_DRAW_TRIFAN    = 0xA0;
    constexpr uint8_t CMD_DRAW_LINES     = 0xA8;
    constexpr uint8_t CMD_DRAW_LINESTRIP = 0xB0;
    constexpr uint8_t CMD_DRAW_POINTS    = 0xB8;
}

// =============================================================================
// Alpha Compare modes
// =============================================================================
enum AlphaCompare : uint8_t {
    ALPHA_NEVER   = 0,
    ALPHA_LESS    = 1,
    ALPHA_EQUAL   = 2,
    ALPHA_LEQUAL  = 3,
    ALPHA_GREATER = 4,
    ALPHA_NEQUAL  = 5,
    ALPHA_GEQUAL  = 6,
    ALPHA_ALWAYS  = 7,
};

enum AlphaOp : uint8_t {
    ALPHA_OP_AND  = 0,
    ALPHA_OP_OR   = 1,
    ALPHA_OP_XOR  = 2,
    ALPHA_OP_XNOR = 3,
};

// =============================================================================
// Fog types
// =============================================================================
enum FogType : uint8_t {
    FOG_NONE   = 0,
    FOG_LINEAR = 2,
    FOG_EXP    = 4,
    FOG_EXP2   = 5,
    FOG_REXP   = 6,
    FOG_REXP2  = 7,
};

// =============================================================================
// TEV Konst Color Selectors
// =============================================================================
enum KonstColorSel : uint8_t {
    KCSEL_1     = 0x00,  // 1.0
    KCSEL_7_8   = 0x01,  // 7/8
    KCSEL_3_4   = 0x02,  // 3/4
    KCSEL_5_8   = 0x03,  // 5/8
    KCSEL_1_2   = 0x04,  // 1/2
    KCSEL_3_8   = 0x05,  // 3/8
    KCSEL_1_4   = 0x06,  // 1/4
    KCSEL_1_8   = 0x07,  // 1/8
    KCSEL_K0    = 0x0C,  // Konst color 0 RGB
    KCSEL_K1    = 0x0D,  // Konst color 1 RGB
    KCSEL_K2    = 0x0E,  // Konst color 2 RGB
    KCSEL_K3    = 0x0F,  // Konst color 3 RGB
    KCSEL_K0_R  = 0x10,
    KCSEL_K1_R  = 0x11,
    KCSEL_K2_R  = 0x12,
    KCSEL_K3_R  = 0x13,
    KCSEL_K0_G  = 0x14,
    KCSEL_K1_G  = 0x15,
    KCSEL_K2_G  = 0x16,
    KCSEL_K3_G  = 0x17,
    KCSEL_K0_B  = 0x18,
    KCSEL_K1_B  = 0x19,
    KCSEL_K2_B  = 0x1A,
    KCSEL_K3_B  = 0x1B,
    KCSEL_K0_A  = 0x1C,
    KCSEL_K1_A  = 0x1D,
    KCSEL_K2_A  = 0x1E,
    KCSEL_K3_A  = 0x1F,
};

enum KonstAlphaSel : uint8_t {
    KASEL_1     = 0x00,
    KASEL_7_8   = 0x01,
    KASEL_3_4   = 0x02,
    KASEL_5_8   = 0x03,
    KASEL_1_2   = 0x04,
    KASEL_3_8   = 0x05,
    KASEL_1_4   = 0x06,
    KASEL_1_8   = 0x07,
    KASEL_K0_R  = 0x10,
    KASEL_K1_R  = 0x11,
    KASEL_K2_R  = 0x12,
    KASEL_K3_R  = 0x13,
    KASEL_K0_G  = 0x14,
    KASEL_K1_G  = 0x15,
    KASEL_K2_G  = 0x16,
    KASEL_K3_G  = 0x17,
    KASEL_K0_B  = 0x18,
    KASEL_K1_B  = 0x19,
    KASEL_K2_B  = 0x1A,
    KASEL_K3_B  = 0x1B,
    KASEL_K0_A  = 0x1C,
    KASEL_K1_A  = 0x1D,
    KASEL_K2_A  = 0x1E,
    KASEL_K3_A  = 0x1F,
};

// =============================================================================
// Indirect Texture definitions (used heavily by Wind Waker for water/ocean)
// =============================================================================

enum IndTexFormat : uint8_t {
    ITF_8  = 0,  // 8-bit
    ITF_5  = 1,  // 5-bit
    ITF_4  = 2,  // 4-bit
    ITF_3  = 3,  // 3-bit
};

enum IndTexBias : uint8_t {
    ITB_NONE = 0,
    ITB_S    = 1,
    ITB_T    = 2,
    ITB_ST   = 3,
    ITB_U    = 4,
    ITB_SU   = 5,
    ITB_TU   = 6,
    ITB_STU  = 7,
};

enum IndTexWrap : uint8_t {
    ITW_OFF  = 0,  // No wrapping
    ITW_256  = 1,
    ITW_128  = 2,
    ITW_64   = 3,
    ITW_32   = 4,
    ITW_16   = 5,
    ITW_0    = 6,  // Clamp to 0
};

enum IndTexMtxId : uint8_t {
    ITM_OFF  = 0,
    ITM_0    = 1,
    ITM_1    = 2,
    ITM_2    = 3,
    ITM_S0   = 5,  // Dynamic S matrix 0
    ITM_S1   = 6,
    ITM_S2   = 7,
    ITM_T0   = 9,  // Dynamic T matrix 0
    ITM_T1   = 10,
    ITM_T2   = 11,
};

// Indirect TEV stage configuration (packed into BP register)
struct IndirectTevStage {
    uint8_t tex_stage;     // Which indirect texture stage (0-3) to sample
    IndTexFormat format;
    IndTexBias bias;
    IndTexMtxId matrix;
    IndTexWrap wrap_s;
    IndTexWrap wrap_t;
    bool add_prev;         // Add previous stage's indirect offset
    bool utc_lod;          // Use unmodified texture coordinates for LOD
};

} // namespace ww::hw
