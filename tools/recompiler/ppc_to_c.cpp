// =============================================================================
// PowerPC to C Static Recompiler
// The magic: translates each PPC instruction into a C expression that
// operates on a PPCContext struct. The generated C code compiles with
// any modern C compiler and runs natively on x86-64.
//
// Example:
//   addi r3, r4, 0x20  ->  ctx->r[3] = (int32_t)ctx->r[4] + 0x20;
//   lwz r5, 0x10(r6)   ->  ctx->r[5] = MEM_READ32(ctx->r[6] + 0x10);
//   bl func_80123456    ->  func_80123456(ctx, mem);
// =============================================================================

#include "ww/ppc_to_c.h"
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <set>
#include <map>

namespace ww {

    void PPCToCEmitter::emit(const char* fmt, ...) {
        for (int i = 0; i < indent_level; i++) fprintf(out, "    ");
        va_list args;
        va_start(args, fmt);
        vfprintf(out, fmt, args);
        va_end(args);
        fprintf(out, "\n");
    }

    void PPCToCEmitter::emit_raw(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vfprintf(out, fmt, args);
        va_end(args);
    }

    void PPCToCEmitter::emit_insn(const PPCInsn& insn) {
        // Comment with original instruction
        emit("// 0x%08X: %s", insn.address, insn.mnemonic.c_str());

        switch (insn.type) {
        // ==== Integer Arithmetic ====
        case PPCInsnType::ADDI:
            if (insn.ra == 0)
                emit("ctx->r[%u] = 0x%X;", insn.rd, (uint32_t)(int32_t)insn.simm);
            else
                emit("ctx->r[%u] = (int32_t)ctx->r[%u] + %d;", insn.rd, insn.ra, insn.simm);
            break;

        case PPCInsnType::ADDIS:
            if (insn.ra == 0)
                emit("ctx->r[%u] = 0x%X;", insn.rd, (uint32_t)((int32_t)insn.simm << 16));
            else
                emit("ctx->r[%u] = (int32_t)ctx->r[%u] + 0x%X;", insn.rd, insn.ra, (uint32_t)((int32_t)insn.simm << 16));
            break;

        case PPCInsnType::ADD:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u];", insn.rd, insn.ra, insn.rb);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;

        case PPCInsnType::ADDC:
            emit("{ uint64_t t = (uint64_t)ctx->r[%u] + (uint64_t)ctx->r[%u]; ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rb, insn.rd);
            break;

        case PPCInsnType::ADDE:
            emit("{ uint64_t t = (uint64_t)ctx->r[%u] + (uint64_t)ctx->r[%u] + ctx->xer_ca(); ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rb, insn.rd);
            break;

        case PPCInsnType::ADDZE:
            emit("{ uint64_t t = (uint64_t)ctx->r[%u] + ctx->xer_ca(); ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rd);
            break;

        case PPCInsnType::SUBF:
            emit("ctx->r[%u] = ctx->r[%u] - ctx->r[%u];", insn.rd, insn.rb, insn.ra);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;

        case PPCInsnType::SUBFC:
            emit("{ uint64_t t = (uint64_t)ctx->r[%u] - (uint64_t)ctx->r[%u]; ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(!(t >> 32)); }",
                 insn.rb, insn.ra, insn.rd);
            break;

        case PPCInsnType::SUBFE:
            emit("{ uint64_t t = (uint64_t)~ctx->r[%u] + (uint64_t)ctx->r[%u] + ctx->xer_ca(); ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rb, insn.rd);
            break;

        case PPCInsnType::SUBFIC:
            emit("{ uint64_t t = (uint64_t)(uint32_t)%d - (uint64_t)ctx->r[%u]; ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(!(t >> 32)); }",
                 insn.simm, insn.ra, insn.rd);
            break;

        case PPCInsnType::NEG:
            emit("ctx->r[%u] = -ctx->r[%u];", insn.rd, insn.ra);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;

        case PPCInsnType::MULLI:
            emit("ctx->r[%u] = (int32_t)ctx->r[%u] * %d;", insn.rd, insn.ra, insn.simm);
            break;

        case PPCInsnType::MULLW:
            emit("ctx->r[%u] = (int32_t)ctx->r[%u] * (int32_t)ctx->r[%u];", insn.rd, insn.ra, insn.rb);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;

        case PPCInsnType::MULHW:
            emit("ctx->r[%u] = (uint32_t)((int64_t)(int32_t)ctx->r[%u] * (int64_t)(int32_t)ctx->r[%u] >> 32);",
                 insn.rd, insn.ra, insn.rb);
            break;

        case PPCInsnType::MULHWU:
            emit("ctx->r[%u] = (uint32_t)((uint64_t)ctx->r[%u] * (uint64_t)ctx->r[%u] >> 32);",
                 insn.rd, insn.ra, insn.rb);
            break;

        case PPCInsnType::DIVW:
            emit("ctx->r[%u] = (int32_t)ctx->r[%u] / (int32_t)ctx->r[%u];", insn.rd, insn.ra, insn.rb);
            break;

        case PPCInsnType::DIVWU:
            emit("ctx->r[%u] = ctx->r[%u] / ctx->r[%u];", insn.rd, insn.ra, insn.rb);
            break;

        // ==== Integer Compare ====
        case PPCInsnType::CMPI:
            emit("{ int32_t a = (int32_t)ctx->r[%u]; int32_t b = %d; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : 2; if (ctx->xer_so()) c |= 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.simm, insn.crfd);
            break;

        case PPCInsnType::CMP:
            emit("{ int32_t a = (int32_t)ctx->r[%u]; int32_t b = (int32_t)ctx->r[%u]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : 2; if (ctx->xer_so()) c |= 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;

        case PPCInsnType::CMPLI:
            emit("{ uint32_t a = ctx->r[%u]; uint32_t b = %u; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : 2; if (ctx->xer_so()) c |= 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.uimm, insn.crfd);
            break;

        case PPCInsnType::CMPL:
            emit("{ uint32_t a = ctx->r[%u]; uint32_t b = ctx->r[%u]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : 2; if (ctx->xer_so()) c |= 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;

        // ==== Integer Logical ====
        case PPCInsnType::ORI:
            emit("ctx->r[%u] = ctx->r[%u] | 0x%X;", insn.ra, insn.rs, insn.uimm);
            break;

        case PPCInsnType::ORIS:
            emit("ctx->r[%u] = ctx->r[%u] | 0x%X;", insn.ra, insn.rs, insn.uimm << 16);
            break;

        case PPCInsnType::ANDI:
            emit("ctx->r[%u] = ctx->r[%u] & 0x%X;", insn.ra, insn.rs, insn.uimm);
            emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::ANDIS:
            emit("ctx->r[%u] = ctx->r[%u] & 0x%X;", insn.ra, insn.rs, insn.uimm << 16);
            emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::XORI:
            emit("ctx->r[%u] = ctx->r[%u] ^ 0x%X;", insn.ra, insn.rs, insn.uimm);
            break;

        case PPCInsnType::XORIS:
            emit("ctx->r[%u] = ctx->r[%u] ^ 0x%X;", insn.ra, insn.rs, insn.uimm << 16);
            break;

        case PPCInsnType::AND:
            emit("ctx->r[%u] = ctx->r[%u] & ctx->r[%u];", insn.ra, insn.rs, insn.rb);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::OR:
            emit("ctx->r[%u] = ctx->r[%u] | ctx->r[%u];", insn.ra, insn.rs, insn.rb);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::XOR:
            emit("ctx->r[%u] = ctx->r[%u] ^ ctx->r[%u];", insn.ra, insn.rs, insn.rb);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::NOR:
            emit("ctx->r[%u] = ~(ctx->r[%u] | ctx->r[%u]);", insn.ra, insn.rs, insn.rb);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::NAND:
            emit("ctx->r[%u] = ~(ctx->r[%u] & ctx->r[%u]);", insn.ra, insn.rs, insn.rb);
            break;

        case PPCInsnType::ANDC:
            emit("ctx->r[%u] = ctx->r[%u] & ~ctx->r[%u];", insn.ra, insn.rs, insn.rb);
            break;

        case PPCInsnType::ORC:
            emit("ctx->r[%u] = ctx->r[%u] | ~ctx->r[%u];", insn.ra, insn.rs, insn.rb);
            break;

        case PPCInsnType::EQV:
            emit("ctx->r[%u] = ~(ctx->r[%u] ^ ctx->r[%u]);", insn.ra, insn.rs, insn.rb);
            break;

        case PPCInsnType::EXTSB:
            emit("ctx->r[%u] = (uint32_t)(int32_t)(int8_t)ctx->r[%u];", insn.ra, insn.rs);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::EXTSH:
            emit("ctx->r[%u] = (uint32_t)(int32_t)(int16_t)ctx->r[%u];", insn.ra, insn.rs);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;

        case PPCInsnType::CNTLZW:
            emit("ctx->r[%u] = PPC_CNTLZW(ctx->r[%u]);", insn.ra, insn.rs);
            break;

        // ==== Shift/Rotate ====
        case PPCInsnType::SLW:
            emit("ctx->r[%u] = (ctx->r[%u] & 0x20) ? 0 : (ctx->r[%u] << (ctx->r[%u] & 0x1F));",
                 insn.ra, insn.rb, insn.rs, insn.rb);
            break;

        case PPCInsnType::SRW:
            emit("ctx->r[%u] = (ctx->r[%u] & 0x20) ? 0 : (ctx->r[%u] >> (ctx->r[%u] & 0x1F));",
                 insn.ra, insn.rb, insn.rs, insn.rb);
            break;

        case PPCInsnType::SRAW:
            emit("{ int32_t s = (int32_t)ctx->r[%u]; uint32_t n = ctx->r[%u] & 0x3F; if (n > 31) { ctx->r[%u] = s >> 31; ctx->set_xer_ca(s < 0); } else { ctx->r[%u] = (uint32_t)(s >> n); ctx->set_xer_ca(s < 0 && (s & ((1 << n) - 1))); } }",
                 insn.rs, insn.rb, insn.ra, insn.ra);
            break;

        case PPCInsnType::SRAWI:
            emit("{ int32_t s = (int32_t)ctx->r[%u]; ctx->r[%u] = (uint32_t)(s >> %u); ctx->set_xer_ca(s < 0 && (s & ((1 << %u) - 1))); }",
                 insn.rs, insn.ra, insn.sh, insn.sh);
            break;

        case PPCInsnType::RLWINM: {
            // Rotate left word immediate then AND with mask
            uint32_t mask = 0;
            for (uint32_t i = insn.mb; i != ((insn.me + 1) & 31) || mask == 0; i = (i + 1) & 31) {
                mask |= (1u << (31 - i));
                if (i == insn.me) break;
            }
            if (insn.sh == 0)
                emit("ctx->r[%u] = ctx->r[%u] & 0x%08X;", insn.ra, insn.rs, mask);
            else
                emit("ctx->r[%u] = PPC_ROTL32(ctx->r[%u], %u) & 0x%08X;", insn.ra, insn.rs, insn.sh, mask);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.ra);
            break;
        }

        case PPCInsnType::RLWIMI: {
            uint32_t mask = 0;
            for (uint32_t i = insn.mb; i != ((insn.me + 1) & 31) || mask == 0; i = (i + 1) & 31) {
                mask |= (1u << (31 - i));
                if (i == insn.me) break;
            }
            emit("ctx->r[%u] = (PPC_ROTL32(ctx->r[%u], %u) & 0x%08X) | (ctx->r[%u] & 0x%08X);",
                 insn.ra, insn.rs, insn.sh, mask, insn.ra, ~mask);
            break;
        }

        case PPCInsnType::RLWNM: {
            uint32_t mask = 0;
            for (uint32_t i = insn.mb; i != ((insn.me + 1) & 31) || mask == 0; i = (i + 1) & 31) {
                mask |= (1u << (31 - i));
                if (i == insn.me) break;
            }
            emit("ctx->r[%u] = PPC_ROTL32(ctx->r[%u], ctx->r[%u] & 0x1F) & 0x%08X;",
                 insn.ra, insn.rs, insn.rb, mask);
            break;
        }

        // ==== Load Integer ====
        case PPCInsnType::LBZ:
            emit("ctx->r[%u] = MEM_READ8(%s + %d);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm);
            break;
        case PPCInsnType::LHZ:
            emit("ctx->r[%u] = MEM_READ16(%s + %d);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm);
            break;
        case PPCInsnType::LHA:
            emit("ctx->r[%u] = (uint32_t)(int32_t)(int16_t)MEM_READ16(%s + %d);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm);
            break;
        case PPCInsnType::LWZ:
            emit("ctx->r[%u] = MEM_READ32(%s + %d);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm);
            break;

        // Load with update
        case PPCInsnType::LWZU:
            emit("ctx->r[%u] += %d; ctx->r[%u] = MEM_READ32(ctx->r[%u]);",
                 insn.ra, insn.simm, insn.rd, insn.ra);
            break;
        case PPCInsnType::LBZU:
            emit("ctx->r[%u] += %d; ctx->r[%u] = MEM_READ8(ctx->r[%u]);",
                 insn.ra, insn.simm, insn.rd, insn.ra);
            break;
        case PPCInsnType::LHZU:
            emit("ctx->r[%u] += %d; ctx->r[%u] = MEM_READ16(ctx->r[%u]);",
                 insn.ra, insn.simm, insn.rd, insn.ra);
            break;

        // Load indexed
        case PPCInsnType::LBZX:
            emit("ctx->r[%u] = MEM_READ8(ctx->r[%u] + ctx->r[%u]);", insn.rd, insn.ra ? insn.ra : 0, insn.rb);
            break;
        case PPCInsnType::LHZX:
            emit("ctx->r[%u] = MEM_READ16(ctx->r[%u] + ctx->r[%u]);", insn.rd, insn.ra ? insn.ra : 0, insn.rb);
            break;
        case PPCInsnType::LWZX:
            emit("ctx->r[%u] = MEM_READ32(ctx->r[%u] + ctx->r[%u]);", insn.rd, insn.ra ? insn.ra : 0, insn.rb);
            break;

        // Load multiple words
        case PPCInsnType::LMW:
            emit("{ uint32_t ea = %s + %d; for (int i = %u; i < 32; i++) { ctx->r[i] = MEM_READ32(ea); ea += 4; } }",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rd);
            break;

        // ==== Store Integer ====
        case PPCInsnType::STB:
            emit("MEM_WRITE8(%s + %d, (uint8_t)ctx->r[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rs);
            break;
        case PPCInsnType::STH:
            emit("MEM_WRITE16(%s + %d, (uint16_t)ctx->r[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rs);
            break;
        case PPCInsnType::STW:
            emit("MEM_WRITE32(%s + %d, ctx->r[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rs);
            break;

        // Store with update (used heavily for stack frames: stwu r1, -X(r1))
        // PPC: EA = rA + d; MEM[EA] = rS; rA = EA (store BEFORE update)
        case PPCInsnType::STWU:
            emit("{ uint32_t ea = ctx->r[%u] + %d; MEM_WRITE32(ea, ctx->r[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.simm, insn.rs, insn.ra);
            break;

        // Store multiple
        case PPCInsnType::STMW:
            emit("{ uint32_t ea = %s + %d; for (int i = %u; i < 32; i++) { MEM_WRITE32(ea, ctx->r[i]); ea += 4; } }",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rs);
            break;

        // ==== Load/Store Float ====
        case PPCInsnType::LFS:
            emit("ctx->f[%u] = (double)MEM_READF32(%s + %d); ctx->ps[%u][0] = ctx->ps[%u][1] = MEM_READF32(%s + %d);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm,
                 insn.rd, insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm);
            break;

        case PPCInsnType::LFD:
            emit("ctx->f[%u] = MEM_READF64(%s + %d);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm);
            break;

        case PPCInsnType::STFS:
            emit("MEM_WRITEF32(%s + %d, (float)ctx->f[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rs);
            break;

        case PPCInsnType::STFD:
            emit("MEM_WRITEF64(%s + %d, ctx->f[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.simm, insn.rs);
            break;

        // ==== Floating Point Arithmetic ====
        case PPCInsnType::FADDS:
            emit("ctx->f[%u] = (double)((float)ctx->f[%u] + (float)ctx->f[%u]); ctx->ps[%u][0] = (float)ctx->f[%u];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.rd);
            break;
        case PPCInsnType::FSUBS:
            emit("ctx->f[%u] = (double)((float)ctx->f[%u] - (float)ctx->f[%u]); ctx->ps[%u][0] = (float)ctx->f[%u];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.rd);
            break;
        case PPCInsnType::FMULS:
            emit("ctx->f[%u] = (double)((float)ctx->f[%u] * (float)ctx->f[%u]); ctx->ps[%u][0] = (float)ctx->f[%u];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rd, insn.rd);
            break;
        case PPCInsnType::FDIVS:
            emit("ctx->f[%u] = (double)((float)ctx->f[%u] / (float)ctx->f[%u]); ctx->ps[%u][0] = (float)ctx->f[%u];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.rd);
            break;
        case PPCInsnType::FADD:
            emit("ctx->f[%u] = ctx->f[%u] + ctx->f[%u];", insn.rd, insn.ra, insn.rb);
            break;
        case PPCInsnType::FSUB:
            emit("ctx->f[%u] = ctx->f[%u] - ctx->f[%u];", insn.rd, insn.ra, insn.rb);
            break;
        case PPCInsnType::FMUL:
            emit("ctx->f[%u] = ctx->f[%u] * ctx->f[%u];", insn.rd, insn.ra, insn.rc_reg);
            break;
        case PPCInsnType::FDIV:
            emit("ctx->f[%u] = ctx->f[%u] / ctx->f[%u];", insn.rd, insn.ra, insn.rb);
            break;

        case PPCInsnType::FMADDS:
            emit("ctx->f[%u] = (double)((float)ctx->f[%u] * (float)ctx->f[%u] + (float)ctx->f[%u]);",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FMSUBS:
            emit("ctx->f[%u] = (double)((float)ctx->f[%u] * (float)ctx->f[%u] - (float)ctx->f[%u]);",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FMADD:
            emit("ctx->f[%u] = ctx->f[%u] * ctx->f[%u] + ctx->f[%u];", insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FMSUB:
            emit("ctx->f[%u] = ctx->f[%u] * ctx->f[%u] - ctx->f[%u];", insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FNMSUBS:
            emit("ctx->f[%u] = (double)-((float)ctx->f[%u] * (float)ctx->f[%u] - (float)ctx->f[%u]);",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FNMADDS:
            emit("ctx->f[%u] = (double)-((float)ctx->f[%u] * (float)ctx->f[%u] + (float)ctx->f[%u]);",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;

        case PPCInsnType::FRES:
            emit("ctx->f[%u] = 1.0 / ctx->f[%u];", insn.rd, insn.rb);
            break;
        case PPCInsnType::FRSQRTE:
            emit("ctx->f[%u] = 1.0 / sqrt(ctx->f[%u]);", insn.rd, insn.rb);
            break;
        case PPCInsnType::FSEL:
            emit("ctx->f[%u] = (ctx->f[%u] >= 0.0) ? ctx->f[%u] : ctx->f[%u];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FMR:
            emit("ctx->f[%u] = ctx->f[%u];", insn.rd, insn.rb);
            break;
        case PPCInsnType::FNEG:
            emit("ctx->f[%u] = -ctx->f[%u];", insn.rd, insn.rb);
            break;
        case PPCInsnType::FABS:
            emit("ctx->f[%u] = fabs(ctx->f[%u]);", insn.rd, insn.rb);
            break;
        case PPCInsnType::FRSP:
            emit("ctx->f[%u] = (double)(float)ctx->f[%u]; ctx->ps[%u][0] = (float)ctx->f[%u];",
                 insn.rd, insn.rb, insn.rd, insn.rd);
            break;
        case PPCInsnType::FCTIWZ:
            emit("{ int32_t v = (int32_t)ctx->f[%u]; uint64_t tmp = (uint64_t)(uint32_t)v; memcpy(&ctx->f[%u], &tmp, 8); }",
                 insn.rb, insn.rd);
            break;

        // ==== Float Compare ====
        case PPCInsnType::FCMPU:
        case PPCInsnType::FCMPO:
            emit("{ double a = ctx->f[%u]; double b = ctx->f[%u]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : (a == b) ? 2 : 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;

        // ==== Paired Singles (the GameCube special sauce) ====
        case PPCInsnType::PS_ADD:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] + ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] + ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.ra, insn.rb);
            break;
        case PPCInsnType::PS_SUB:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] - ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] - ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.ra, insn.rb);
            break;
        case PPCInsnType::PS_MUL:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rd, insn.ra, insn.rc_reg);
            break;
        case PPCInsnType::PS_DIV:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] / ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] / ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.ra, insn.rb);
            break;
        case PPCInsnType::PS_MADD:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][0] + ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][1] + ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_MSUB:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][0] - ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][1] - ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_MULS0:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][0];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rd, insn.ra, insn.rc_reg);
            break;
        case PPCInsnType::PS_MULS1:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][1]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rd, insn.ra, insn.rc_reg);
            break;
        case PPCInsnType::PS_MERGE00:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][0];",
                 insn.rd, insn.ra, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_MERGE01:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_MERGE10:
            emit("ctx->ps[%u][0] = ctx->ps[%u][1]; ctx->ps[%u][1] = ctx->ps[%u][0];",
                 insn.rd, insn.ra, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_MERGE11:
            emit("ctx->ps[%u][0] = ctx->ps[%u][1]; ctx->ps[%u][1] = ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_MR:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1];",
                 insn.rd, insn.rb, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_NEG:
            emit("ctx->ps[%u][0] = -ctx->ps[%u][0]; ctx->ps[%u][1] = -ctx->ps[%u][1];",
                 insn.rd, insn.rb, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_ABS:
            emit("ctx->ps[%u][0] = fabsf(ctx->ps[%u][0]); ctx->ps[%u][1] = fabsf(ctx->ps[%u][1]);",
                 insn.rd, insn.rb, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_RES:
            emit("ctx->ps[%u][0] = 1.0f / ctx->ps[%u][0]; ctx->ps[%u][1] = 1.0f / ctx->ps[%u][1];",
                 insn.rd, insn.rb, insn.rd, insn.rb);
            break;

        // ==== Condition Register Operations ====
        case PPCInsnType::CRXOR:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, a ^ b); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CROR:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, a | b); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CRAND:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, a & b); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CRANDC:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, a & !b); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CREQV:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, !(a ^ b)); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CRNAND:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, !(a & b)); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CRNOR:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, !(a | b)); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::CRORC:
            emit("{ bool a = ctx->get_cr_bit(%u); bool b = ctx->get_cr_bit(%u); ctx->set_cr_bit(%u, a | !b); }",
                 PPC_CRBA(insn.raw), PPC_CRBB(insn.raw), PPC_CRBD(insn.raw));
            break;
        case PPCInsnType::MCRF:
            emit("ctx->set_cr_field(%u, ctx->get_cr_field(%u));", insn.crfd, insn.crfs);
            break;

        // ==== Integer Arithmetic (missing) ====
        case PPCInsnType::ADDIC:
            emit("{ uint64_t t = (uint64_t)ctx->r[%u] + (uint64_t)(uint32_t)%d; ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.simm, insn.rd);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;
        case PPCInsnType::ADDME:
            emit("{ uint64_t t = (uint64_t)ctx->r[%u] + (uint64_t)0xFFFFFFFF + ctx->xer_ca(); ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rd);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;
        case PPCInsnType::SUBFZE:
            emit("{ uint64_t t = (uint64_t)~ctx->r[%u] + ctx->xer_ca(); ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rd);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;
        case PPCInsnType::SUBFME:
            emit("{ uint64_t t = (uint64_t)~ctx->r[%u] + (uint64_t)0xFFFFFFFF + ctx->xer_ca(); ctx->r[%u] = (uint32_t)t; ctx->set_xer_ca(t >> 32); }",
                 insn.ra, insn.rd);
            if (insn.rc) emit("ctx->update_cr0((int32_t)ctx->r[%u]);", insn.rd);
            break;

        // ==== Store Indexed ====
        case PPCInsnType::STBX:
            emit("MEM_WRITE8(%s + ctx->r[%u], (uint8_t)ctx->r[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rs);
            break;
        case PPCInsnType::STHX:
            emit("MEM_WRITE16(%s + ctx->r[%u], (uint16_t)ctx->r[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rs);
            break;
        case PPCInsnType::STWX:
            emit("MEM_WRITE32(%s + ctx->r[%u], ctx->r[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rs);
            break;

        // Store with update (indexed) — EA = rA + rB; MEM[EA] = rS; rA = EA
        case PPCInsnType::STBUX:
            emit("{ uint32_t ea = ctx->r[%u] + ctx->r[%u]; MEM_WRITE8(ea, (uint8_t)ctx->r[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.rb, insn.rs, insn.ra);
            break;
        case PPCInsnType::STHUX:
            emit("{ uint32_t ea = ctx->r[%u] + ctx->r[%u]; MEM_WRITE16(ea, (uint16_t)ctx->r[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.rb, insn.rs, insn.ra);
            break;
        case PPCInsnType::STWUX:
            emit("{ uint32_t ea = ctx->r[%u] + ctx->r[%u]; MEM_WRITE32(ea, ctx->r[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.rb, insn.rs, insn.ra);
            break;

        // Store byte-reversed
        case PPCInsnType::STHBRX:
            emit("{ uint16_t v = (uint16_t)ctx->r[%u]; MEM_WRITE16(%s + ctx->r[%u], (v >> 8) | (v << 8)); }",
                 insn.rs, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb);
            break;
        case PPCInsnType::STWBRX:
            emit("{ uint32_t v = ctx->r[%u]; MEM_WRITE32(%s + ctx->r[%u], (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24)); }",
                 insn.rs, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb);
            break;

        // Store with update (displacement) — EA = rA + d; MEM[EA] = rS; rA = EA
        case PPCInsnType::STBU:
            emit("{ uint32_t ea = ctx->r[%u] + %d; MEM_WRITE8(ea, (uint8_t)ctx->r[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.simm, insn.rs, insn.ra);
            break;
        case PPCInsnType::STHU:
            emit("{ uint32_t ea = ctx->r[%u] + %d; MEM_WRITE16(ea, (uint16_t)ctx->r[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.simm, insn.rs, insn.ra);
            break;

        // ==== Load Indexed (missing) ====
        case PPCInsnType::LHAX:
            emit("ctx->r[%u] = (uint32_t)(int32_t)(int16_t)MEM_READ16(%s + ctx->r[%u]);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb);
            break;
        case PPCInsnType::LHAUX:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u]; ctx->r[%u] = (uint32_t)(int32_t)(int16_t)MEM_READ16(ctx->r[%u]);",
                 insn.ra, insn.ra, insn.rb, insn.rd, insn.ra);
            break;

        // Load with update (indexed)
        case PPCInsnType::LBZUX:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u]; ctx->r[%u] = MEM_READ8(ctx->r[%u]);",
                 insn.ra, insn.ra, insn.rb, insn.rd, insn.ra);
            break;
        case PPCInsnType::LHZUX:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u]; ctx->r[%u] = MEM_READ16(ctx->r[%u]);",
                 insn.ra, insn.ra, insn.rb, insn.rd, insn.ra);
            break;
        case PPCInsnType::LWZUX:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u]; ctx->r[%u] = MEM_READ32(ctx->r[%u]);",
                 insn.ra, insn.ra, insn.rb, insn.rd, insn.ra);
            break;

        // Load with update (displacement, missing forms)
        case PPCInsnType::LHAU:
            emit("ctx->r[%u] += %d; ctx->r[%u] = (uint32_t)(int32_t)(int16_t)MEM_READ16(ctx->r[%u]);",
                 insn.ra, insn.simm, insn.rd, insn.ra);
            break;

        // Load byte-reversed
        case PPCInsnType::LHBRX:
            emit("{ uint16_t v = MEM_READ16(%s + ctx->r[%u]); ctx->r[%u] = (v >> 8) | (v << 8); }",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rd);
            break;
        case PPCInsnType::LWBRX:
            emit("{ uint32_t v = MEM_READ32(%s + ctx->r[%u]); ctx->r[%u] = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rd);
            break;

        // ==== Float Load/Store Indexed ====
        case PPCInsnType::LFSX:
            emit("ctx->f[%u] = (double)MEM_READF32(%s + ctx->r[%u]); ctx->ps[%u][0] = ctx->ps[%u][1] = MEM_READF32(%s + ctx->r[%u]);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb,
                 insn.rd, insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb);
            break;
        case PPCInsnType::LFDX:
            emit("ctx->f[%u] = MEM_READF64(%s + ctx->r[%u]);",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb);
            break;
        case PPCInsnType::STFSX:
            emit("MEM_WRITEF32(%s + ctx->r[%u], (float)ctx->f[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rs);
            break;
        case PPCInsnType::STFDX:
            emit("MEM_WRITEF64(%s + ctx->r[%u], ctx->f[%u]);",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rs);
            break;

        // Float load/store with update
        case PPCInsnType::LFSU:
            emit("ctx->r[%u] += %d; ctx->f[%u] = (double)MEM_READF32(ctx->r[%u]); ctx->ps[%u][0] = ctx->ps[%u][1] = (float)ctx->f[%u];",
                 insn.ra, insn.simm, insn.rd, insn.ra, insn.rd, insn.rd, insn.rd);
            break;
        case PPCInsnType::LFDU:
            emit("ctx->r[%u] += %d; ctx->f[%u] = MEM_READF64(ctx->r[%u]);",
                 insn.ra, insn.simm, insn.rd, insn.ra);
            break;
        case PPCInsnType::STFSU:
            emit("{ uint32_t ea = ctx->r[%u] + %d; MEM_WRITEF32(ea, (float)ctx->f[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.simm, insn.rs, insn.ra);
            break;
        case PPCInsnType::STFDU:
            emit("{ uint32_t ea = ctx->r[%u] + %d; MEM_WRITEF64(ea, ctx->f[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.simm, insn.rs, insn.ra);
            break;

        // Float indexed with update
        case PPCInsnType::LFSUX:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u]; ctx->f[%u] = (double)MEM_READF32(ctx->r[%u]); ctx->ps[%u][0] = ctx->ps[%u][1] = (float)ctx->f[%u];",
                 insn.ra, insn.ra, insn.rb, insn.rd, insn.ra, insn.rd, insn.rd, insn.rd);
            break;
        case PPCInsnType::LFDUX:
            emit("ctx->r[%u] = ctx->r[%u] + ctx->r[%u]; ctx->f[%u] = MEM_READF64(ctx->r[%u]);",
                 insn.ra, insn.ra, insn.rb, insn.rd, insn.ra);
            break;
        case PPCInsnType::STFSUX:
            emit("{ uint32_t ea = ctx->r[%u] + ctx->r[%u]; MEM_WRITEF32(ea, (float)ctx->f[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.rb, insn.rs, insn.ra);
            break;
        case PPCInsnType::STFDUX:
            emit("{ uint32_t ea = ctx->r[%u] + ctx->r[%u]; MEM_WRITEF64(ea, ctx->f[%u]); ctx->r[%u] = ea; }",
                 insn.ra, insn.rb, insn.rs, insn.ra);
            break;

        // ==== Float (missing) ====
        case PPCInsnType::FNABS:
            emit("ctx->f[%u] = -fabs(ctx->f[%u]);", insn.rd, insn.rb);
            break;
        case PPCInsnType::FNMADD:
            emit("ctx->f[%u] = -(ctx->f[%u] * ctx->f[%u] + ctx->f[%u]);", insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FNMSUB:
            emit("ctx->f[%u] = -(ctx->f[%u] * ctx->f[%u] - ctx->f[%u]);", insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::FCTIW:
            emit("{ int32_t v = (int32_t)ctx->f[%u]; uint64_t tmp = (uint64_t)(uint32_t)v; memcpy(&ctx->f[%u], &tmp, 8); }",
                 insn.rb, insn.rd);
            break;
        case PPCInsnType::MFFS:
            emit("{ uint64_t v = (uint64_t)ctx->fpscr; memcpy(&ctx->f[%u], &v, 8); }", insn.rd);
            break;
        case PPCInsnType::MTFSF:
            emit("{ uint64_t v; memcpy(&v, &ctx->f[%u], 8); ctx->fpscr = (uint32_t)v; }", insn.rb);
            break;

        // ==== Paired Singles Quantized Load/Store ====
        // For GQR=0 (most common), quantization type is float and scale is 1.
        // We emit a runtime helper call that reads the GQR register.
        case PPCInsnType::PSQ_L:
            if (insn.psq_w) {
                emit("ctx->ps[%u][0] = PSQ_LOAD_ONE(%s + %d, ctx->gqr[%u]); ctx->ps[%u][1] = 1.0f;",
                     insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0",
                     insn.simm, insn.psq_i, insn.rd);
            } else {
                emit("PSQ_LOAD_PAIR(&ctx->ps[%u][0], &ctx->ps[%u][1], %s + %d, ctx->gqr[%u]);",
                     insn.rd, insn.rd,
                     insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0",
                     insn.simm, insn.psq_i);
            }
            break;
        case PPCInsnType::PSQ_LU:
            emit("ctx->r[%u] += %d;", insn.ra, insn.simm);
            if (insn.psq_w) {
                emit("ctx->ps[%u][0] = PSQ_LOAD_ONE(ctx->r[%u], ctx->gqr[%u]); ctx->ps[%u][1] = 1.0f;",
                     insn.rd, insn.ra, insn.psq_i, insn.rd);
            } else {
                emit("PSQ_LOAD_PAIR(&ctx->ps[%u][0], &ctx->ps[%u][1], ctx->r[%u], ctx->gqr[%u]);",
                     insn.rd, insn.rd, insn.ra, insn.psq_i);
            }
            break;
        case PPCInsnType::PSQ_ST:
            if (insn.psq_w) {
                emit("PSQ_STORE_ONE(ctx->ps[%u][0], %s + %d, ctx->gqr[%u]);",
                     insn.rs, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0",
                     insn.simm, insn.psq_i);
            } else {
                emit("PSQ_STORE_PAIR(ctx->ps[%u][0], ctx->ps[%u][1], %s + %d, ctx->gqr[%u]);",
                     insn.rs, insn.rs,
                     insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0",
                     insn.simm, insn.psq_i);
            }
            break;
        case PPCInsnType::PSQ_STU:
            emit("ctx->r[%u] += %d;", insn.ra, insn.simm);
            if (insn.psq_w) {
                emit("PSQ_STORE_ONE(ctx->ps[%u][0], ctx->r[%u], ctx->gqr[%u]);",
                     insn.rs, insn.ra, insn.psq_i);
            } else {
                emit("PSQ_STORE_PAIR(ctx->ps[%u][0], ctx->ps[%u][1], ctx->r[%u], ctx->gqr[%u]);",
                     insn.rs, insn.rs, insn.ra, insn.psq_i);
            }
            break;

        // ==== Paired Singles (missing operations) ====
        case PPCInsnType::PS_NMADD:
            emit("ctx->ps[%u][0] = -(ctx->ps[%u][0] * ctx->ps[%u][0] + ctx->ps[%u][0]); ctx->ps[%u][1] = -(ctx->ps[%u][1] * ctx->ps[%u][1] + ctx->ps[%u][1]);",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_NMSUB:
            emit("ctx->ps[%u][0] = -(ctx->ps[%u][0] * ctx->ps[%u][0] - ctx->ps[%u][0]); ctx->ps[%u][1] = -(ctx->ps[%u][1] * ctx->ps[%u][1] - ctx->ps[%u][1]);",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_SEL:
            emit("ctx->ps[%u][0] = (ctx->ps[%u][0] >= 0.0f) ? ctx->ps[%u][0] : ctx->ps[%u][0]; ctx->ps[%u][1] = (ctx->ps[%u][1] >= 0.0f) ? ctx->ps[%u][1] : ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_SUM0:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] + ctx->ps[%u][1]; ctx->ps[%u][1] = ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rb, insn.rd, insn.rc_reg);
            break;
        case PPCInsnType::PS_SUM1:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][0] + ctx->ps[%u][1];",
                 insn.rd, insn.rc_reg, insn.rd, insn.ra, insn.rb);
            break;
        case PPCInsnType::PS_MADDS0:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][0] + ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][0] + ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_MADDS1:
            emit("ctx->ps[%u][0] = ctx->ps[%u][0] * ctx->ps[%u][1] + ctx->ps[%u][0]; ctx->ps[%u][1] = ctx->ps[%u][1] * ctx->ps[%u][1] + ctx->ps[%u][1];",
                 insn.rd, insn.ra, insn.rc_reg, insn.rb, insn.rd, insn.ra, insn.rc_reg, insn.rb);
            break;
        case PPCInsnType::PS_NABS:
            emit("ctx->ps[%u][0] = -fabsf(ctx->ps[%u][0]); ctx->ps[%u][1] = -fabsf(ctx->ps[%u][1]);",
                 insn.rd, insn.rb, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_RSQRTE:
            emit("ctx->ps[%u][0] = 1.0f / sqrtf(ctx->ps[%u][0]); ctx->ps[%u][1] = 1.0f / sqrtf(ctx->ps[%u][1]);",
                 insn.rd, insn.rb, insn.rd, insn.rb);
            break;
        case PPCInsnType::PS_CMPU0:
            emit("{ float a = ctx->ps[%u][0]; float b = ctx->ps[%u][0]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : (a == b) ? 2 : 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;
        case PPCInsnType::PS_CMPU1:
            emit("{ float a = ctx->ps[%u][1]; float b = ctx->ps[%u][1]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : (a == b) ? 2 : 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;
        case PPCInsnType::PS_CMPO0:
            emit("{ float a = ctx->ps[%u][0]; float b = ctx->ps[%u][0]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : (a == b) ? 2 : 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;
        case PPCInsnType::PS_CMPO1:
            emit("{ float a = ctx->ps[%u][1]; float b = ctx->ps[%u][1]; uint32_t c = (a < b) ? 8 : (a > b) ? 4 : (a == b) ? 2 : 1; ctx->set_cr_field(%u, c); }",
                 insn.ra, insn.rb, insn.crfd);
            break;

        // ==== Sync/Atomic ====
        case PPCInsnType::LWARX:
            emit("ctx->r[%u] = MEM_READ32(%s + ctx->r[%u]); // lwarx (reservation ignored)",
                 insn.rd, insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb);
            break;
        case PPCInsnType::STWCX:
            emit("MEM_WRITE32(%s + ctx->r[%u], ctx->r[%u]); ctx->set_cr_field(0, 0x2 | (ctx->xer_so() ? 1 : 0)); // stwcx. (always succeeds)",
                 insn.ra ? (std::string("ctx->r[") + std::to_string(insn.ra) + "]").c_str() : "0", insn.rb, insn.rs);
            break;

        // ==== System ====
        case PPCInsnType::SC:
            emit("// sc (system call - not expected in game code)");
            break;
        case PPCInsnType::RFI:
            emit("return; // rfi (return from interrupt)");
            break;
        case PPCInsnType::TWI:
        case PPCInsnType::TW:
            emit("// %s (trap - ignored)", insn.mnemonic.c_str());
            break;
        case PPCInsnType::MFMSR:
            emit("ctx->r[%u] = 0; // mfmsr (supervisor, no-op)", insn.rd);
            break;
        case PPCInsnType::MTMSR:
            emit("// mtmsr r%u (supervisor, no-op)", insn.rs);
            break;
        case PPCInsnType::DCBI:
        case PPCInsnType::DCBZ_L:
        case PPCInsnType::TLBIE:
        case PPCInsnType::TLBSYNC:
            emit("// %s (supervisor/cache, no-op)", insn.mnemonic.c_str());
            break;

        // ==== SPR Access ====
        case PPCInsnType::MFSPR:
            if (insn.spr == 8)       emit("ctx->r[%u] = ctx->lr;", insn.rd);
            else if (insn.spr == 9)  emit("ctx->r[%u] = ctx->ctr;", insn.rd);
            else if (insn.spr == 1)  emit("ctx->r[%u] = ctx->xer;", insn.rd);
            else if (insn.spr >= 912 && insn.spr <= 919)
                emit("ctx->r[%u] = ctx->gqr[%u];", insn.rd, insn.spr - 912);
            else if (insn.spr == 268) emit("ctx->r[%u] = PPC_MFTBL(); // mftb (TBL)", insn.rd);
            else if (insn.spr == 269) emit("ctx->r[%u] = PPC_MFTBU(); // mftbu (TBU)", insn.rd);
            else emit("ctx->r[%u] = 0; // mfspr %u (unhandled)", insn.rd, insn.spr);
            break;

        case PPCInsnType::MTSPR:
            // Note: for mtspr, the source register is in the rd field position (bits 21-25)
            if (insn.spr == 8)       emit("ctx->lr = ctx->r[%u];", insn.rd);
            else if (insn.spr == 9)  emit("ctx->ctr = ctx->r[%u];", insn.rd);
            else if (insn.spr == 1)  emit("ctx->xer = ctx->r[%u];", insn.rd);
            else if (insn.spr >= 912 && insn.spr <= 919)
                emit("ctx->gqr[%u] = ctx->r[%u];", insn.spr - 912, insn.rd);
            else emit("// mtspr %u, r%u (unhandled)", insn.spr, insn.rd);
            break;

        case PPCInsnType::MFCR:
            emit("ctx->r[%u] = ctx->cr;", insn.rd);
            break;

        case PPCInsnType::MTCRF:
            emit("{ uint32_t mask = 0; uint32_t crm = (0x%08X >> 12) & 0xFF; for (int i = 0; i < 8; i++) if (crm & (1 << (7-i))) mask |= 0xF << (28 - i*4); ctx->cr = (ctx->cr & ~mask) | (ctx->r[%u] & mask); }",
                 insn.raw, insn.rs);
            break;

        // ==== Branch (handled at block level, but emit for completeness) ====
        case PPCInsnType::B:
            if (insn.link) {
                emit("ctx->lr = 0x%08Xu; func_%08X(ctx, mem); // bl", insn.address + 4, insn.branch_target);
            } else {
                emit("goto label_%08X; // b", insn.branch_target);
            }
            break;

        case PPCInsnType::BC: {
            // Conditional branch: BO field encodes condition
            // bit 4 (0x10): 1=don't test CR  bit 3 (0x08): CR sense (1=true)
            // bit 2 (0x04): 1=don't decrement CTR  bit 1 (0x02): CTR sense (1=branch if CTR==0)
            uint32_t bo = insn.bo;
            uint32_t bi = insn.bi;
            bool skip_ctr = (bo & 0x04) != 0;
            bool skip_cr  = (bo & 0x10) != 0;

            if (skip_ctr && skip_cr) {
                // BO=20: unconditional (shouldn't normally appear as bc, but handle it)
                emit("goto label_%08X; // bc (always)", insn.branch_target);
            } else if (skip_ctr && !skip_cr) {
                // Pure CR test (most common: BO=4 bf, BO=12 bt)
                bool cr_sense = (bo & 0x08) != 0;
                if (cr_sense)
                    emit("if (ctx->get_cr_bit(%u)) goto label_%08X; // bt cr%u", bi, insn.branch_target, bi);
                else
                    emit("if (!ctx->get_cr_bit(%u)) goto label_%08X; // bf cr%u", bi, insn.branch_target, bi);
            } else if (!skip_ctr && skip_cr) {
                // CTR decrement only (BO=16 bdnz, BO=18 bdz)
                bool ctr_zero = (bo & 0x02) != 0;
                if (ctr_zero)
                    emit("if (--ctx->ctr == 0) goto label_%08X; // bdz", insn.branch_target);
                else
                    emit("if (--ctx->ctr != 0) goto label_%08X; // bdnz", insn.branch_target);
            } else {
                // Both CTR and CR test (rare)
                bool cr_sense = (bo & 0x08) != 0;
                bool ctr_zero = (bo & 0x02) != 0;
                emit("{ ctx->ctr--; if (ctx->ctr %s 0 && %sctx->get_cr_bit(%u)) goto label_%08X; } // bc %u, %u",
                     ctr_zero ? "==" : "!=", cr_sense ? "" : "!", bi, insn.branch_target, bo, bi);
            }
            break;
        }

        case PPCInsnType::BCLR: {
            if (insn.is_return()) {
                emit("return; // blr");
            } else if (insn.link) {
                emit("{ uint32_t target = ctx->lr; ctx->lr = 0x%08Xu; CALL_INDIRECT(target, ctx, mem); } // blrl", insn.address + 4);
            } else {
                // Conditional return via LR
                uint32_t bo = insn.bo;
                uint32_t bi = insn.bi;
                bool skip_ctr = (bo & 0x04) != 0;
                bool skip_cr  = (bo & 0x10) != 0;

                if (skip_ctr && !skip_cr) {
                    bool cr_sense = (bo & 0x08) != 0;
                    if (cr_sense)
                        emit("if (ctx->get_cr_bit(%u)) return; // bclr bt cr%u", bi, bi);
                    else
                        emit("if (!ctx->get_cr_bit(%u)) return; // bclr bf cr%u", bi, bi);
                } else if (!skip_ctr && skip_cr) {
                    bool ctr_zero = (bo & 0x02) != 0;
                    if (ctr_zero)
                        emit("if (--ctx->ctr == 0) return; // bdzlr");
                    else
                        emit("if (--ctx->ctr != 0) return; // bdnzlr");
                } else {
                    emit("return; // bclr (unconditional fallback)");
                }
            }
            break;
        }

        case PPCInsnType::BCCTR:
            if (insn.link) {
                emit("ctx->lr = 0x%08Xu; CALL_INDIRECT(ctx->ctr, ctx, mem); // bctrl", insn.address + 4);
            } else {
                // Switch table: dispatch to a label within this function based on CTR value
                emit("// bctr — switch table dispatch");
                emit("switch (ctx->ctr) {");
                for (uint32_t addr : block_addrs) {
                    emit("    case 0x%08Xu: goto label_%08X;", addr, addr);
                }
                emit("    default: CALL_INDIRECT(ctx->ctr, ctx, mem); break; // fallback: tail call");
                emit("}");
            }
            break;

        // ==== No-ops and cache ops (safe to ignore) ====
        case PPCInsnType::NOP:
        case PPCInsnType::SYNC:
        case PPCInsnType::ISYNC:
        case PPCInsnType::EIEIO:
        case PPCInsnType::DCBF:
        case PPCInsnType::DCBST:
        case PPCInsnType::DCBT:
        case PPCInsnType::DCBTST:
        case PPCInsnType::ICBI:
            emit("// %s (no-op on x86)", insn.mnemonic.c_str());
            break;

        case PPCInsnType::DCBZ:
            emit("memset((void*)((uintptr_t)mem->translate(ctx->r[%u] + ctx->r[%u]) & ~31), 0, 32); // dcbz",
                 insn.ra, insn.rb);
            break;

        default:
            emit("// UNIMPLEMENTED: %s (0x%08X)", insn.mnemonic.c_str(), insn.raw);
            break;
        }
    }

void emit_file_header(FILE* out) {
    fprintf(out, "// =============================================================\n");
    fprintf(out, "// Wind Waker Static Recompilation - Auto-generated C code\n");
    fprintf(out, "// Source: GameCube DOL PowerPC 750CXe (Gekko)\n");
    fprintf(out, "// Target: x86-64 Windows (MSVC/Clang/GCC)\n");
    fprintf(out, "// DO NOT EDIT - Generated by ww_recompiler\n");
    fprintf(out, "// =============================================================\n\n");
    fprintf(out, "#include \"recomp_common.h\"\n");
    fprintf(out, "#include \"recomp_funcs.h\"\n\n");
}

} // namespace ww
