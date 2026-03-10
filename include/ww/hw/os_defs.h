#pragma once
// =============================================================================
// GameCube Dolphin OS Definitions
// Struct layouts and constants for OS-level data structures that the game
// expects to find in memory. Derived from public SDK documentation,
// libogc (zlib license), and Pureikyubu (CC0).
// =============================================================================

#include <cstdint>

namespace ww::os {

// =============================================================================
// OSThread - Thread control block
// The game stores and reads these structures in emulated memory.
// We only need the layout for memory compatibility; actual threading
// is handled natively.
// =============================================================================
struct OSThread {
    // The full struct is 0x318 bytes. Key fields:
    // 0x000: OSContext (register save area, 0x2C8 bytes)
    // 0x2C8: state (u16)
    // 0x2CA: attributes (u16)
    // 0x2CC: suspend count (s32)
    // 0x2D0: effective priority (s32)
    // 0x2D4: base priority (s32)
    // 0x2D8: exit value (void*)
    // 0x2DC-0x2E3: thread queue links
    // 0x2F4: stack base (void*)
    // 0x2F8: stack end (void*)
    static constexpr uint32_t SIZE = 0x318;

    // State values
    static constexpr uint16_t STATE_READY    = 1;
    static constexpr uint16_t STATE_RUNNING  = 2;
    static constexpr uint16_t STATE_WAITING  = 4;
    static constexpr uint16_t STATE_MORIBUND = 8;
};

// Offsets within OSThread for field access
namespace thread_off {
    constexpr uint32_t STATE          = 0x2C8;
    constexpr uint32_t ATTR           = 0x2CA;
    constexpr uint32_t SUSPEND_COUNT  = 0x2CC;
    constexpr uint32_t PRIORITY       = 0x2D0;
    constexpr uint32_t BASE_PRIORITY  = 0x2D4;
    constexpr uint32_t EXIT_VALUE     = 0x2D8;
    constexpr uint32_t QUEUE_NEXT     = 0x2DC;
    constexpr uint32_t QUEUE_PREV     = 0x2E0;
    constexpr uint32_t LINK_NEXT      = 0x2E4;
    constexpr uint32_t LINK_PREV      = 0x2E8;
    constexpr uint32_t STACK_BASE     = 0x2F4;
    constexpr uint32_t STACK_END      = 0x2F8;
}

// =============================================================================
// OSContext - CPU register save area
// =============================================================================
namespace ctx_off {
    constexpr uint32_t GPR_BASE       = 0x00C;  // r0-r31 (32 x 4 bytes)
    constexpr uint32_t CR             = 0x080;
    constexpr uint32_t LR             = 0x084;
    constexpr uint32_t CTR            = 0x088;
    constexpr uint32_t XER            = 0x08C;
    constexpr uint32_t FPR_BASE       = 0x090;  // f0-f31 (32 x 8 bytes)
    constexpr uint32_t FPSCR          = 0x190;
    constexpr uint32_t SRR0           = 0x198;  // Machine status save/restore register
    constexpr uint32_t SRR1           = 0x19C;
    constexpr uint32_t STATE          = 0x1A2;  // Context state flags
    constexpr uint32_t GQR_BASE       = 0x1A8;  // GQR0-GQR7 (8 x 4 bytes)
    constexpr uint32_t PS_BASE        = 0x1C8;  // Paired singles ps0-ps31 (32 x 8 bytes)
    constexpr uint32_t SIZE           = 0x2C8;
}

// =============================================================================
// OSAlarm
// =============================================================================
struct OSAlarm {
    static constexpr uint32_t SIZE = 0x28;
};

// =============================================================================
// OSMutex
// =============================================================================
struct OSMutex {
    static constexpr uint32_t SIZE = 0x18;
};

// =============================================================================
// OSMessageQueue
// =============================================================================
struct OSMessageQueue {
    static constexpr uint32_t SIZE = 0x20;
};

// =============================================================================
// DVD structures
// =============================================================================
struct DVDFileInfo {
    // 0x00: DVDCommandBlock (0x30 bytes)
    // 0x30: start address (file offset on disc)
    // 0x34: length
    // 0x38: callback
    static constexpr uint32_t SIZE         = 0x3C;
    static constexpr uint32_t OFF_START    = 0x30;
    static constexpr uint32_t OFF_LENGTH   = 0x34;
    static constexpr uint32_t OFF_CALLBACK = 0x38;
};

// =============================================================================
// Heap structures
// =============================================================================

// OS heap header (in emulated memory)
struct OSHeapHeader {
    // Heap descriptor stored in low memory heap array
    static constexpr uint32_t SIZE = 0x10;
    // 0x00: size (total heap size)
    // 0x04: free list head
    // 0x08: allocated list head
    // 0x0C: padding
};

// Free block header (16 bytes, stored at the start of each free region)
struct OSHeapCell {
    // 0x00: prev (pointer to previous free cell)
    // 0x04: next (pointer to next free cell)
    // 0x08: size (size of this free block including header)
    static constexpr uint32_t SIZE = 0x10;
};

// =============================================================================
// Card (memory card) structures
// =============================================================================
struct CARDFileInfo {
    static constexpr uint32_t SIZE = 0x14;
    // 0x00: channel (s32)
    // 0x04: file number (s32)
    // 0x08: offset
    // 0x0C: length
    // 0x10: iBlock (internal)
};

// =============================================================================
// PAD (controller) status - matches the in-memory layout
// =============================================================================
struct PADStatus {
    uint16_t button;
    int8_t   stick_x;
    int8_t   stick_y;
    int8_t   substick_x;
    int8_t   substick_y;
    uint8_t  trigger_l;
    uint8_t  trigger_r;
    uint8_t  analog_a;
    uint8_t  analog_b;
    int8_t   err;
    uint8_t  padding;

    static constexpr uint32_t SIZE = 12;
};
static_assert(sizeof(PADStatus) == 12, "PADStatus must be 12 bytes");

// =============================================================================
// Error codes
// =============================================================================
constexpr int32_t OS_ERROR_OK        =  0;
constexpr int32_t OS_ERROR_INVALID   = -1;
constexpr int32_t OS_ERROR_NO_MEM    = -2;
constexpr int32_t OS_ERROR_BUSY      = -3;

constexpr int32_t DVD_RESULT_OK      =  0;
constexpr int32_t DVD_RESULT_FATAL   = -1;
constexpr int32_t DVD_RESULT_IGNORED = -2;
constexpr int32_t DVD_RESULT_CANCELED = -3;

constexpr int32_t CARD_RESULT_READY       =  0;
constexpr int32_t CARD_RESULT_BUSY        = -1;
constexpr int32_t CARD_RESULT_WRONGDEVICE = -2;
constexpr int32_t CARD_RESULT_NOCARD      = -3;
constexpr int32_t CARD_RESULT_NOFILE      = -4;
constexpr int32_t CARD_RESULT_IOERROR     = -5;
constexpr int32_t CARD_RESULT_BROKEN      = -6;
constexpr int32_t CARD_RESULT_EXIST       = -7;
constexpr int32_t CARD_RESULT_NOENT       = -8;
constexpr int32_t CARD_RESULT_INSSPACE    = -9;
constexpr int32_t CARD_RESULT_NOPERM      = -10;
constexpr int32_t CARD_RESULT_LIMIT       = -11;
constexpr int32_t CARD_RESULT_NAMETOOLONG = -12;

} // namespace ww::os
