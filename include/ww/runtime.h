#pragma once
// =============================================================================
// Wind Waker Static Recompilation - Runtime
// This is the CPU context and memory system that recompiled C code runs against.
// Every recompiled function takes a PPCContext* and operates on it.
// =============================================================================

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>

namespace ww {

// ---- PowerPC CPU Context --------------------------------------------------
// Mirrors the Gekko processor state. Recompiled code reads/writes this.

struct alignas(16) PPCContext {
    // General Purpose Registers (r0-r31)
    uint32_t r[32];

    // Floating Point Registers (f0-f31) - each is 64-bit double
    double f[32];

    // Paired Singles registers (ps0/ps1 for each FPR)
    // ps[i][0] = high (same as f[i] single-precision part)
    // ps[i][1] = low (paired single extension)
    float ps[32][2];

    // Condition Register (8 x 4-bit fields = 32 bits)
    // CR0-CR7, each has LT/GT/EQ/SO bits
    uint32_t cr;

    // Special Purpose Registers
    uint32_t lr;        // Link Register (return address)
    uint32_t ctr;       // Count Register (loop counter / indirect branch)
    uint32_t xer;       // Fixed-point Exception Register (CA, OV, SO, byte count)

    // Floating Point Status and Control Register
    uint32_t fpscr;

    // GQR0-GQR7: Graphics Quantization Registers (for paired singles load/store)
    uint32_t gqr[8];

    // Program counter (for debugging / indirect branches)
    uint32_t pc;

    // ---- Helpers ----------------------------------------------------------

    // CR field access (field 0-7)
    uint32_t get_cr_field(int field) const {
        return (cr >> (28 - field * 4)) & 0xF;
    }
    void set_cr_field(int field, uint32_t val) {
        uint32_t shift = 28 - field * 4;
        cr = (cr & ~(0xF << shift)) | ((val & 0xF) << shift);
    }

    // CR bit access (bit 0-31)
    bool get_cr_bit(int bit) const {
        return (cr >> (31 - bit)) & 1;
    }
    void set_cr_bit(int bit, bool val) {
        if (val) cr |= (1 << (31 - bit));
        else     cr &= ~(1 << (31 - bit));
    }

    // XER fields
    bool xer_so() const { return (xer >> 31) & 1; }
    bool xer_ov() const { return (xer >> 30) & 1; }
    bool xer_ca() const { return (xer >> 29) & 1; }
    void set_xer_ca(bool v) { if (v) xer |= (1 << 29); else xer &= ~(1 << 29); }
    void set_xer_ov(bool v) {
        if (v) { xer |= (1 << 30) | (1 << 31); } // OV also sets SO (sticky)
        else   { xer &= ~(1 << 30); }
    }

    // Update CR0 after integer operation (Rc=1)
    void update_cr0(int32_t result) {
        uint32_t val = 0;
        if (result < 0)       val = 0x8; // LT
        else if (result > 0)  val = 0x4; // GT
        else                  val = 0x2; // EQ
        if (xer_so())         val |= 0x1; // SO
        set_cr_field(0, val);
    }

    // Initialize to power-on state
    void reset() {
        memset(this, 0, sizeof(*this));
        // Stack pointer (r1) will be set by the loader
    }
};

// ---- Memory System --------------------------------------------------------
// Big-endian memory with address translation.
// GameCube physical memory: 24MB main + 16MB ARAM
// Virtual: 0x80000000 - 0x817FFFFF (cached)
//          0xC0000000 - 0xC17FFFFF (uncached, same physical)

struct Memory {
    static constexpr uint32_t MAIN_RAM_SIZE  = 24 * 1024 * 1024;  // 24 MB
    static constexpr uint32_t MAIN_RAM_BASE  = 0x80000000;
    static constexpr uint32_t MAIN_RAM_END   = MAIN_RAM_BASE + MAIN_RAM_SIZE;
    static constexpr uint32_t UNCACHED_BASE  = 0xC0000000;

    uint8_t* ram = nullptr;

    bool init();
    void shutdown();

    // Translate virtual address to host pointer
    uint8_t* translate(uint32_t addr);
    const uint8_t* translate(uint32_t addr) const;

    // Big-endian read/write (what recompiled code uses)
    uint8_t  read8(uint32_t addr) const;
    uint16_t read16(uint32_t addr) const;
    uint32_t read32(uint32_t addr) const;
    uint64_t read64(uint32_t addr) const;
    float    readf32(uint32_t addr) const;
    double   readf64(uint32_t addr) const;

    void write8(uint32_t addr, uint8_t val);
    void write16(uint32_t addr, uint16_t val);
    void write32(uint32_t addr, uint32_t val);
    void write64(uint32_t addr, uint64_t val);
    void writef32(uint32_t addr, float val);
    void writef64(uint32_t addr, double val);
};

// ---- Function Table -------------------------------------------------------
// Maps GameCube addresses to recompiled C functions.
// Used for indirect calls (blr to function pointer, virtual methods, etc.)

using RecompiledFunc = void(*)(PPCContext* ctx, Memory* mem);

struct FuncTable {
    std::unordered_map<uint32_t, RecompiledFunc> table;

    void register_func(uint32_t gc_addr, RecompiledFunc func);
    RecompiledFunc lookup(uint32_t gc_addr) const;

    // Call a function by its GameCube address
    void call(uint32_t gc_addr, PPCContext* ctx, Memory* mem) const;
};

// ---- Global runtime state -------------------------------------------------
extern PPCContext g_ctx;
extern Memory     g_mem;
extern FuncTable  g_func_table;

bool runtime_init();
void runtime_shutdown();

// ---- OS HLE ---------------------------------------------------------------
void init_low_memory(Memory* mem);
void register_os_functions();
RecompiledFunc lookup_os_func(const char* name);
void set_game_root(const std::string& path);

} // namespace ww
