// =============================================================================
// Memory System Implementation
// Emulates GameCube's 24MB main RAM with big-endian access
// Virtual addresses: 0x80000000-0x817FFFFF (cached)
//                    0xC0000000-0xC17FFFFF (uncached, maps to same physical)
// =============================================================================

#include "ww/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ww {

Memory g_mem;

bool Memory::init() {
    ram = (uint8_t*)calloc(1, MAIN_RAM_SIZE);
    if (!ram) {
        fprintf(stderr, "[Memory] Failed to allocate %u MB\n", MAIN_RAM_SIZE / (1024 * 1024));
        return false;
    }
    printf("[Memory] Allocated %u MB main RAM\n", MAIN_RAM_SIZE / (1024 * 1024));
    return true;
}

void Memory::shutdown() {
    if (ram) {
        free(ram);
        ram = nullptr;
    }
}

uint8_t* Memory::translate(uint32_t addr) {
    // Cached region: 0x80000000 - 0x817FFFFF
    if (addr >= MAIN_RAM_BASE && addr < MAIN_RAM_END) {
        return ram + (addr - MAIN_RAM_BASE);
    }
    // Uncached region: 0xC0000000 - 0xC17FFFFF (same physical memory)
    if (addr >= UNCACHED_BASE && addr < UNCACHED_BASE + MAIN_RAM_SIZE) {
        return ram + (addr - UNCACHED_BASE);
    }
    // Out of range
    fprintf(stderr, "[Memory] Bad address: 0x%08X\n", addr);
    return ram; // Don't crash, return base (will produce wrong results but won't segfault)
}

const uint8_t* Memory::translate(uint32_t addr) const {
    return const_cast<Memory*>(this)->translate(addr);
}

// Big-endian reads (GameCube byte order)
uint8_t Memory::read8(uint32_t addr) const {
    return *translate(addr);
}

uint16_t Memory::read16(uint32_t addr) const {
    const uint8_t* p = translate(addr);
    return ((uint16_t)p[0] << 8) | p[1];
}

uint32_t Memory::read32(uint32_t addr) const {
    const uint8_t* p = translate(addr);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

uint64_t Memory::read64(uint32_t addr) const {
    return ((uint64_t)read32(addr) << 32) | read32(addr + 4);
}

float Memory::readf32(uint32_t addr) const {
    uint32_t v = read32(addr);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

double Memory::readf64(uint32_t addr) const {
    uint64_t v = read64(addr);
    double d;
    memcpy(&d, &v, 8);
    return d;
}

// Big-endian writes
void Memory::write8(uint32_t addr, uint8_t val) {
    *translate(addr) = val;
}

void Memory::write16(uint32_t addr, uint16_t val) {
    uint8_t* p = translate(addr);
    p[0] = (uint8_t)(val >> 8);
    p[1] = (uint8_t)val;
}

void Memory::write32(uint32_t addr, uint32_t val) {
    uint8_t* p = translate(addr);
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)val;
}

void Memory::write64(uint32_t addr, uint64_t val) {
    write32(addr, (uint32_t)(val >> 32));
    write32(addr + 4, (uint32_t)val);
}

void Memory::writef32(uint32_t addr, float val) {
    uint32_t v;
    memcpy(&v, &val, 4);
    write32(addr, v);
}

void Memory::writef64(uint32_t addr, double val) {
    uint64_t v;
    memcpy(&v, &val, 8);
    write64(addr, v);
}

} // namespace ww
