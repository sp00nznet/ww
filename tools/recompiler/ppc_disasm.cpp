// =============================================================================
// PowerPC 750CXe (Gekko) Disassembler
// Decodes raw 32-bit instruction words into structured PPCInsn objects
// Covers: integer, float, paired singles, branch, system, load/store
// =============================================================================

#include "ww/ppc.h"
#include <cstdio>

namespace ww {

bool PPCInsn::is_branch() const {
    return type == PPCInsnType::B || type == PPCInsnType::BC ||
           type == PPCInsnType::BCLR || type == PPCInsnType::BCCTR;
}

bool PPCInsn::is_call() const {
    return is_branch() && link;
}

bool PPCInsn::is_return() const {
    // blr: bclr with BO=20 (always), no link
    return type == PPCInsnType::BCLR && !link && (bo & 0x14) == 0x14;
}

bool PPCInsn::is_conditional() const {
    if (type == PPCInsnType::BC || type == PPCInsnType::BCLR || type == PPCInsnType::BCCTR) {
        // BO field bit 4 = don't test CR, bit 2 = don't decrement CTR
        return (bo & 0x14) != 0x14;
    }
    return false;
}

// Decode primary opcode 4: Paired Singles and some PS loads/stores
static PPCInsn decode_op4(uint32_t raw, uint32_t addr) {
    PPCInsn insn{};
    insn.raw = raw;
    insn.address = addr;
    insn.rd = PPC_FD(raw);
    insn.ra = PPC_FA(raw);
    insn.rb = PPC_FB(raw);
    insn.rc_reg = PPC_FC(raw);
    insn.rc = PPC_RC_FLAG(raw);

    uint32_t xo = PPC_XO(raw);
    // Sub-opcode in bits 25-30 (6-bit) for some PS instructions
    uint32_t xo5 = (raw >> 1) & 0x1F;

    // Check 10-bit XO first
    switch (xo) {
        case 40:  insn.type = PPCInsnType::PS_NEG;     insn.mnemonic = "ps_neg"; return insn;
        case 72:  insn.type = PPCInsnType::PS_MR;      insn.mnemonic = "ps_mr"; return insn;
        case 136: insn.type = PPCInsnType::PS_NABS;    insn.mnemonic = "ps_nabs"; return insn;
        case 264: insn.type = PPCInsnType::PS_ABS;     insn.mnemonic = "ps_abs"; return insn;
        case 528: insn.type = PPCInsnType::PS_MERGE00; insn.mnemonic = "ps_merge00"; return insn;
        case 560: insn.type = PPCInsnType::PS_MERGE01; insn.mnemonic = "ps_merge01"; return insn;
        case 592: insn.type = PPCInsnType::PS_MERGE10; insn.mnemonic = "ps_merge10"; return insn;
        case 624: insn.type = PPCInsnType::PS_MERGE11; insn.mnemonic = "ps_merge11"; return insn;
        case 0:   insn.type = PPCInsnType::PS_CMPU0;   insn.mnemonic = "ps_cmpu0"; return insn;
        case 32:  insn.type = PPCInsnType::PS_CMPO0;   insn.mnemonic = "ps_cmpo0"; return insn;
        case 64:  insn.type = PPCInsnType::PS_CMPU1;   insn.mnemonic = "ps_cmpu1"; return insn;
        case 96:  insn.type = PPCInsnType::PS_CMPO1;   insn.mnemonic = "ps_cmpo1"; return insn;
        case 24:  insn.type = PPCInsnType::PS_RES;     insn.mnemonic = "ps_res"; return insn;
        case 26:  insn.type = PPCInsnType::PS_RSQRTE;  insn.mnemonic = "ps_rsqrte"; return insn;
        default: break;
    }

    // Check 5-bit XO (bits 26-30)
    switch (xo5) {
        case 10: insn.type = PPCInsnType::PS_SUM0;    insn.mnemonic = "ps_sum0"; return insn;
        case 11: insn.type = PPCInsnType::PS_SUM1;    insn.mnemonic = "ps_sum1"; return insn;
        case 12: insn.type = PPCInsnType::PS_MULS0;   insn.mnemonic = "ps_muls0"; return insn;
        case 13: insn.type = PPCInsnType::PS_MULS1;   insn.mnemonic = "ps_muls1"; return insn;
        case 14: insn.type = PPCInsnType::PS_MADDS0;  insn.mnemonic = "ps_madds0"; return insn;
        case 15: insn.type = PPCInsnType::PS_MADDS1;  insn.mnemonic = "ps_madds1"; return insn;
        case 18: insn.type = PPCInsnType::PS_DIV;     insn.mnemonic = "ps_div"; return insn;
        case 20: insn.type = PPCInsnType::PS_SUB;     insn.mnemonic = "ps_sub"; return insn;
        case 21: insn.type = PPCInsnType::PS_ADD;     insn.mnemonic = "ps_add"; return insn;
        case 23: insn.type = PPCInsnType::PS_SEL;     insn.mnemonic = "ps_sel"; return insn;
        case 25: insn.type = PPCInsnType::PS_MUL;     insn.mnemonic = "ps_mul"; return insn;
        case 28: insn.type = PPCInsnType::PS_MSUB;    insn.mnemonic = "ps_msub"; return insn;
        case 29: insn.type = PPCInsnType::PS_MADD;    insn.mnemonic = "ps_madd"; return insn;
        case 30: insn.type = PPCInsnType::PS_NMSUB;   insn.mnemonic = "ps_nmsub"; return insn;
        case 31: insn.type = PPCInsnType::PS_NMADD;   insn.mnemonic = "ps_nmadd"; return insn;
        default: break;
    }

    insn.type = PPCInsnType::UNKNOWN;
    insn.mnemonic = "ps_???";
    return insn;
}

PPCInsn ppc_disasm(uint32_t raw, uint32_t address) {
    PPCInsn insn{};
    insn.raw = raw;
    insn.address = address;
    insn.type = PPCInsnType::UNKNOWN;
    insn.mnemonic = "???";
    insn.link = false;
    insn.aa = false;
    insn.rc = false;
    insn.oe = false;

    uint32_t op = PPC_OP(raw);

    switch (op) {
    // ---- Branch instructions ----
    case 18: { // b, bl, ba, bla
        insn.type = PPCInsnType::B;
        insn.link = PPC_LK(raw);
        insn.aa = PPC_AA(raw);
        insn.branch_target = PPC_LI(raw);
        if (!insn.aa) insn.branch_target += address;
        insn.mnemonic = insn.link ? "bl" : "b";
        break;
    }
    case 16: { // bc, bcl, bca, bcla
        insn.type = PPCInsnType::BC;
        insn.bo = PPC_BO(raw);
        insn.bi = PPC_BI(raw);
        insn.link = PPC_LK(raw);
        insn.aa = PPC_AA(raw);
        insn.branch_target = PPC_BD(raw);
        if (!insn.aa) insn.branch_target += address;
        insn.mnemonic = insn.link ? "bcl" : "bc";
        break;
    }

    // ---- Integer Arithmetic (immediate) ----
    case 14: { // addi
        insn.type = PPCInsnType::ADDI;
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.mnemonic = (insn.ra == 0) ? "li" : "addi";
        break;
    }
    case 15: { // addis
        insn.type = PPCInsnType::ADDIS;
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.mnemonic = (insn.ra == 0) ? "lis" : "addis";
        break;
    }
    case 12: { // addic
        insn.type = PPCInsnType::ADDIC;
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.mnemonic = "addic";
        break;
    }
    case 13: { // addic.
        insn.type = PPCInsnType::ADDIC;
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.rc = true;
        insn.mnemonic = "addic.";
        break;
    }
    case 8: { // subfic
        insn.type = PPCInsnType::SUBFIC;
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.mnemonic = "subfic";
        break;
    }
    case 7: { // mulli
        insn.type = PPCInsnType::MULLI;
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.mnemonic = "mulli";
        break;
    }

    // ---- Integer Compare ----
    case 11: { // cmpi (cmpwi)
        insn.type = PPCInsnType::CMPI;
        insn.crfd = PPC_CRFD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        insn.mnemonic = "cmpwi";
        break;
    }
    case 10: { // cmpli (cmplwi)
        insn.type = PPCInsnType::CMPLI;
        insn.crfd = PPC_CRFD(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        insn.mnemonic = "cmplwi";
        break;
    }

    // ---- Integer Logical (immediate) ----
    case 28: { // andi.
        insn.type = PPCInsnType::ANDI;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        insn.rc = true;
        insn.mnemonic = "andi.";
        break;
    }
    case 29: { // andis.
        insn.type = PPCInsnType::ANDIS;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        insn.rc = true;
        insn.mnemonic = "andis.";
        break;
    }
    case 24: { // ori (also nop when ori r0,r0,0)
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        if (insn.rs == 0 && insn.ra == 0 && insn.uimm == 0) {
            insn.type = PPCInsnType::NOP;
            insn.mnemonic = "nop";
        } else {
            insn.type = PPCInsnType::ORI;
            insn.mnemonic = "ori";
        }
        break;
    }
    case 25: { // oris
        insn.type = PPCInsnType::ORIS;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        insn.mnemonic = "oris";
        break;
    }
    case 26: { // xori
        insn.type = PPCInsnType::XORI;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        insn.mnemonic = "xori";
        break;
    }
    case 27: { // xoris
        insn.type = PPCInsnType::XORIS;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.uimm = PPC_UIMM(raw);
        insn.mnemonic = "xoris";
        break;
    }

    // ---- Rotate/Shift (immediate forms) ----
    case 21: { // rlwinm
        insn.type = PPCInsnType::RLWINM;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.sh = PPC_SH(raw);
        insn.mb = PPC_MB(raw);
        insn.me = PPC_ME(raw);
        insn.rc = PPC_RC_FLAG(raw);
        insn.mnemonic = "rlwinm";
        break;
    }
    case 20: { // rlwimi
        insn.type = PPCInsnType::RLWIMI;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.sh = PPC_SH(raw);
        insn.mb = PPC_MB(raw);
        insn.me = PPC_ME(raw);
        insn.rc = PPC_RC_FLAG(raw);
        insn.mnemonic = "rlwimi";
        break;
    }
    case 23: { // rlwnm
        insn.type = PPCInsnType::RLWNM;
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.rb = PPC_RB(raw);
        insn.mb = PPC_MB(raw);
        insn.me = PPC_ME(raw);
        insn.rc = PPC_RC_FLAG(raw);
        insn.mnemonic = "rlwnm";
        break;
    }

    // ---- Load Integer ----
    case 34: insn.type = PPCInsnType::LBZ;  insn.mnemonic = "lbz";  goto load_imm;
    case 35: insn.type = PPCInsnType::LBZU; insn.mnemonic = "lbzu"; goto load_imm;
    case 40: insn.type = PPCInsnType::LHZ;  insn.mnemonic = "lhz";  goto load_imm;
    case 41: insn.type = PPCInsnType::LHZU; insn.mnemonic = "lhzu"; goto load_imm;
    case 42: insn.type = PPCInsnType::LHA;  insn.mnemonic = "lha";  goto load_imm;
    case 43: insn.type = PPCInsnType::LHAU; insn.mnemonic = "lhau"; goto load_imm;
    case 32: insn.type = PPCInsnType::LWZ;  insn.mnemonic = "lwz";  goto load_imm;
    case 33: insn.type = PPCInsnType::LWZU; insn.mnemonic = "lwzu"; goto load_imm;
    load_imm:
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        break;

    // ---- Store Integer ----
    case 38: insn.type = PPCInsnType::STB;  insn.mnemonic = "stb";  goto store_imm;
    case 39: insn.type = PPCInsnType::STBU; insn.mnemonic = "stbu"; goto store_imm;
    case 44: insn.type = PPCInsnType::STH;  insn.mnemonic = "sth";  goto store_imm;
    case 45: insn.type = PPCInsnType::STHU; insn.mnemonic = "sthu"; goto store_imm;
    case 36: insn.type = PPCInsnType::STW;  insn.mnemonic = "stw";  goto store_imm;
    case 37: insn.type = PPCInsnType::STWU; insn.mnemonic = "stwu"; goto store_imm;
    store_imm:
        insn.rs = PPC_RS(raw);
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        break;

    // ---- Load/Store Multiple ----
    case 46: insn.type = PPCInsnType::LMW;  insn.mnemonic = "lmw";
        insn.rd = PPC_RD(raw); insn.ra = PPC_RA(raw); insn.simm = PPC_SIMM(raw); break;
    case 47: insn.type = PPCInsnType::STMW; insn.mnemonic = "stmw";
        insn.rs = PPC_RS(raw); insn.ra = PPC_RA(raw); insn.simm = PPC_SIMM(raw); break;

    // ---- Load/Store Float ----
    case 48: insn.type = PPCInsnType::LFS;  insn.mnemonic = "lfs";  goto load_imm;
    case 49: insn.type = PPCInsnType::LFSU; insn.mnemonic = "lfsu"; goto load_imm;
    case 50: insn.type = PPCInsnType::LFD;  insn.mnemonic = "lfd";  goto load_imm;
    case 51: insn.type = PPCInsnType::LFDU; insn.mnemonic = "lfdu"; goto load_imm;
    case 52: insn.type = PPCInsnType::STFS; insn.mnemonic = "stfs"; goto store_imm;
    case 53: insn.type = PPCInsnType::STFSU;insn.mnemonic = "stfsu";goto store_imm;
    case 54: insn.type = PPCInsnType::STFD; insn.mnemonic = "stfd"; goto store_imm;
    case 55: insn.type = PPCInsnType::STFDU;insn.mnemonic = "stfdu";goto store_imm;

    // ---- Paired Singles Load/Store (quantized) ----
    case 56: { // psq_l
        insn.type = PPCInsnType::PSQ_L;
        insn.mnemonic = "psq_l";
        insn.rd = PPC_FD(raw);
        insn.ra = PPC_RA(raw);
        insn.psq_w = (raw >> 15) & 1;       // W bit: 0=paired, 1=single
        insn.psq_i = (raw >> 12) & 0x7;     // GQR index (0-7)
        insn.simm = (int16_t)((raw & 0xFFF) << 4) >> 4; // 12-bit signed
        break;
    }
    case 57: { // psq_lu
        insn.type = PPCInsnType::PSQ_LU;
        insn.mnemonic = "psq_lu";
        insn.rd = PPC_FD(raw);
        insn.ra = PPC_RA(raw);
        insn.psq_w = (raw >> 15) & 1;
        insn.psq_i = (raw >> 12) & 0x7;
        insn.simm = (int16_t)((raw & 0xFFF) << 4) >> 4;
        break;
    }
    case 60: { // psq_st
        insn.type = PPCInsnType::PSQ_ST;
        insn.mnemonic = "psq_st";
        insn.rs = PPC_FS(raw);
        insn.ra = PPC_RA(raw);
        insn.psq_w = (raw >> 15) & 1;
        insn.psq_i = (raw >> 12) & 0x7;
        insn.simm = (int16_t)((raw & 0xFFF) << 4) >> 4;
        break;
    }
    case 61: { // psq_stu
        insn.type = PPCInsnType::PSQ_STU;
        insn.mnemonic = "psq_stu";
        insn.rs = PPC_FS(raw);
        insn.ra = PPC_RA(raw);
        insn.psq_w = (raw >> 15) & 1;
        insn.psq_i = (raw >> 12) & 0x7;
        insn.simm = (int16_t)((raw & 0xFFF) << 4) >> 4;
        break;
    }

    // ---- Opcode 4: Paired Singles ----
    case 4:
        return decode_op4(raw, address);

    // ---- Opcode 19: Condition register and branch-to-LR/CTR ----
    case 19: {
        uint32_t xo = PPC_XO(raw);
        switch (xo) {
        case 16: // bclr (blr)
            insn.type = PPCInsnType::BCLR;
            insn.bo = PPC_BO(raw);
            insn.bi = PPC_BI(raw);
            insn.link = PPC_LK(raw);
            insn.mnemonic = insn.link ? "bclrl" : "bclr";
            if ((insn.bo & 0x14) == 0x14 && !insn.link) insn.mnemonic = "blr";
            break;
        case 528: // bcctr (bctr)
            insn.type = PPCInsnType::BCCTR;
            insn.bo = PPC_BO(raw);
            insn.bi = PPC_BI(raw);
            insn.link = PPC_LK(raw);
            insn.mnemonic = insn.link ? "bcctrl" : "bcctr";
            if ((insn.bo & 0x14) == 0x14) insn.mnemonic = insn.link ? "bctrl" : "bctr";
            break;
        case 257: insn.type = PPCInsnType::CRAND;  insn.mnemonic = "crand"; break;
        case 129: insn.type = PPCInsnType::CRANDC; insn.mnemonic = "crandc"; break;
        case 289: insn.type = PPCInsnType::CREQV;  insn.mnemonic = "creqv"; break;
        case 225: insn.type = PPCInsnType::CRNAND; insn.mnemonic = "crnand"; break;
        case 33:  insn.type = PPCInsnType::CRNOR;  insn.mnemonic = "crnor"; break;
        case 449: insn.type = PPCInsnType::CROR;   insn.mnemonic = "cror"; break;
        case 417: insn.type = PPCInsnType::CRORC;  insn.mnemonic = "crorc"; break;
        case 193: insn.type = PPCInsnType::CRXOR;  insn.mnemonic = "crxor"; break;
        case 0:   insn.type = PPCInsnType::MCRF;   insn.mnemonic = "mcrf"; break;
        case 150: insn.type = PPCInsnType::ISYNC;  insn.mnemonic = "isync"; break;
        case 50:  insn.type = PPCInsnType::RFI;    insn.mnemonic = "rfi"; break;
        default:
            insn.type = PPCInsnType::UNKNOWN;
            insn.mnemonic = "op19_???";
        }
        break;
    }

    // ---- Opcode 31: Extended integer ops ----
    case 31: {
        uint32_t xo = PPC_XO(raw);
        insn.rd = PPC_RD(raw);
        insn.ra = PPC_RA(raw);
        insn.rb = PPC_RB(raw);
        insn.rc = PPC_RC_FLAG(raw);
        insn.oe = PPC_OE(raw);

        // For XO-form with 9-bit extended opcode
        uint32_t xo9 = (raw >> 1) & 0x1FF;

        switch (xo) {
        // Arithmetic
        case 266: insn.type = PPCInsnType::ADD;    insn.mnemonic = "add"; break;
        case 10:  insn.type = PPCInsnType::ADDC;   insn.mnemonic = "addc"; break;
        case 138: insn.type = PPCInsnType::ADDE;   insn.mnemonic = "adde"; break;
        case 202: insn.type = PPCInsnType::ADDZE;  insn.mnemonic = "addze"; break;
        case 234: insn.type = PPCInsnType::ADDME;  insn.mnemonic = "addme"; break;
        case 40:  insn.type = PPCInsnType::SUBF;   insn.mnemonic = "subf"; break;
        case 8:   insn.type = PPCInsnType::SUBFC;  insn.mnemonic = "subfc"; break;
        case 136: insn.type = PPCInsnType::SUBFE;  insn.mnemonic = "subfe"; break;
        case 232: insn.type = PPCInsnType::SUBFZE; insn.mnemonic = "subfze"; break;
        case 200: insn.type = PPCInsnType::SUBFME; insn.mnemonic = "subfme"; break;
        case 104: insn.type = PPCInsnType::NEG;    insn.mnemonic = "neg"; break;
        case 235: insn.type = PPCInsnType::MULLW;  insn.mnemonic = "mullw"; break;
        case 75:  insn.type = PPCInsnType::MULHW;  insn.mnemonic = "mulhw"; break;
        case 11:  insn.type = PPCInsnType::MULHWU; insn.mnemonic = "mulhwu"; break;
        case 491: insn.type = PPCInsnType::DIVW;   insn.mnemonic = "divw"; break;
        case 459: insn.type = PPCInsnType::DIVWU;  insn.mnemonic = "divwu"; break;

        // Compare
        case 0:   insn.type = PPCInsnType::CMP;    insn.mnemonic = "cmpw";
                  insn.crfd = PPC_CRFD(raw); break;
        case 32:  insn.type = PPCInsnType::CMPL;   insn.mnemonic = "cmplw";
                  insn.crfd = PPC_CRFD(raw); break;

        // Logical
        case 28:  insn.type = PPCInsnType::AND;    insn.mnemonic = "and"; break;
        case 444: insn.type = PPCInsnType::OR;     insn.mnemonic = "or";
                  if (insn.rs == insn.rb) insn.mnemonic = "mr"; break;
        case 316: insn.type = PPCInsnType::XOR;    insn.mnemonic = "xor"; break;
        case 476: insn.type = PPCInsnType::NAND;   insn.mnemonic = "nand"; break;
        case 124: insn.type = PPCInsnType::NOR;    insn.mnemonic = "nor";
                  if (insn.rs == insn.rb) insn.mnemonic = "not"; break;
        case 284: insn.type = PPCInsnType::EQV;    insn.mnemonic = "eqv"; break;
        case 60:  insn.type = PPCInsnType::ANDC;   insn.mnemonic = "andc"; break;
        case 412: insn.type = PPCInsnType::ORC;    insn.mnemonic = "orc"; break;
        case 954: insn.type = PPCInsnType::EXTSB;  insn.mnemonic = "extsb"; break;
        case 922: insn.type = PPCInsnType::EXTSH;  insn.mnemonic = "extsh"; break;
        case 26:  insn.type = PPCInsnType::CNTLZW; insn.mnemonic = "cntlzw"; break;

        // Shift
        case 24:  insn.type = PPCInsnType::SLW;    insn.mnemonic = "slw"; break;
        case 536: insn.type = PPCInsnType::SRW;    insn.mnemonic = "srw"; break;
        case 792: insn.type = PPCInsnType::SRAW;   insn.mnemonic = "sraw"; break;
        case 824: insn.type = PPCInsnType::SRAWI;  insn.mnemonic = "srawi";
                  insn.sh = PPC_SH(raw); break;

        // Load indexed
        case 87:  insn.type = PPCInsnType::LBZX;   insn.mnemonic = "lbzx"; break;
        case 119: insn.type = PPCInsnType::LBZUX;  insn.mnemonic = "lbzux"; break;
        case 279: insn.type = PPCInsnType::LHZX;   insn.mnemonic = "lhzx"; break;
        case 311: insn.type = PPCInsnType::LHZUX;  insn.mnemonic = "lhzux"; break;
        case 343: insn.type = PPCInsnType::LHAX;   insn.mnemonic = "lhax"; break;
        case 375: insn.type = PPCInsnType::LHAUX;  insn.mnemonic = "lhaux"; break;
        case 23:  insn.type = PPCInsnType::LWZX;   insn.mnemonic = "lwzx"; break;
        case 55:  insn.type = PPCInsnType::LWZUX;  insn.mnemonic = "lwzux"; break;
        case 790: insn.type = PPCInsnType::LHBRX;  insn.mnemonic = "lhbrx"; break;
        case 534: insn.type = PPCInsnType::LWBRX;  insn.mnemonic = "lwbrx"; break;
        case 20:  insn.type = PPCInsnType::LWARX;  insn.mnemonic = "lwarx"; break;

        // Store indexed
        case 215: insn.type = PPCInsnType::STBX;   insn.mnemonic = "stbx"; break;
        case 247: insn.type = PPCInsnType::STBUX;  insn.mnemonic = "stbux"; break;
        case 407: insn.type = PPCInsnType::STHX;   insn.mnemonic = "sthx"; break;
        case 439: insn.type = PPCInsnType::STHUX;  insn.mnemonic = "sthux"; break;
        case 151: insn.type = PPCInsnType::STWX;   insn.mnemonic = "stwx"; break;
        case 183: insn.type = PPCInsnType::STWUX;  insn.mnemonic = "stwux"; break;
        case 918: insn.type = PPCInsnType::STHBRX; insn.mnemonic = "sthbrx"; break;
        case 662: insn.type = PPCInsnType::STWBRX; insn.mnemonic = "stwbrx"; break;

        // Float indexed
        case 535: insn.type = PPCInsnType::LFSX;   insn.mnemonic = "lfsx"; break;
        case 567: insn.type = PPCInsnType::LFSUX;  insn.mnemonic = "lfsux"; break;
        case 599: insn.type = PPCInsnType::LFDX;   insn.mnemonic = "lfdx"; break;
        case 631: insn.type = PPCInsnType::LFDUX;  insn.mnemonic = "lfdux"; break;
        case 663: insn.type = PPCInsnType::STFSX;  insn.mnemonic = "stfsx"; break;
        case 695: insn.type = PPCInsnType::STFSUX; insn.mnemonic = "stfsux"; break;
        case 727: insn.type = PPCInsnType::STFDX;  insn.mnemonic = "stfdx"; break;
        case 759: insn.type = PPCInsnType::STFDUX; insn.mnemonic = "stfdux"; break;

        // SPR access
        case 339: insn.type = PPCInsnType::MFSPR;  insn.mnemonic = "mfspr";
                  insn.spr = PPC_SPR(raw); break;
        case 467: insn.type = PPCInsnType::MTSPR;  insn.mnemonic = "mtspr";
                  insn.spr = PPC_SPR(raw); break;
        case 19:  insn.type = PPCInsnType::MFCR;   insn.mnemonic = "mfcr"; break;
        case 144: insn.type = PPCInsnType::MTCRF;  insn.mnemonic = "mtcrf"; break;

        // Sync/cache
        case 598: insn.type = PPCInsnType::SYNC;   insn.mnemonic = "sync"; break;
        case 854: insn.type = PPCInsnType::EIEIO;  insn.mnemonic = "eieio"; break;
        case 982: insn.type = PPCInsnType::ICBI;   insn.mnemonic = "icbi"; break;
        case 86:  insn.type = PPCInsnType::DCBF;   insn.mnemonic = "dcbf"; break;
        case 54:  insn.type = PPCInsnType::DCBST;  insn.mnemonic = "dcbst"; break;
        case 278: insn.type = PPCInsnType::DCBT;   insn.mnemonic = "dcbt"; break;
        case 246: insn.type = PPCInsnType::DCBTST; insn.mnemonic = "dcbtst"; break;
        case 1014: insn.type = PPCInsnType::DCBZ;  insn.mnemonic = "dcbz"; break;

        // stwcx.
        case 150: insn.type = PPCInsnType::STWCX;  insn.mnemonic = "stwcx."; break;

        // Supervisor-level
        case 83:  insn.type = PPCInsnType::MFMSR;  insn.mnemonic = "mfmsr"; break;
        case 146: insn.type = PPCInsnType::MTMSR;  insn.mnemonic = "mtmsr"; break;
        case 210: insn.type = PPCInsnType::SYNC;   insn.mnemonic = "mtsrin"; break; // segment reg, no-op
        case 595: insn.type = PPCInsnType::SYNC;   insn.mnemonic = "mfsrin"; break; // segment reg, no-op
        case 242: insn.type = PPCInsnType::SYNC;   insn.mnemonic = "mfsr"; break;   // segment reg, no-op
        case 370: insn.type = PPCInsnType::TLBIE;  insn.mnemonic = "tlbia"; break;
        case 306: insn.type = PPCInsnType::TLBIE;  insn.mnemonic = "tlbie"; break;
        case 566: insn.type = PPCInsnType::TLBSYNC; insn.mnemonic = "tlbsync"; break;
        case 978: insn.type = PPCInsnType::DCBZ_L; insn.mnemonic = "dcbz_l"; break;
        case 470: insn.type = PPCInsnType::DCBI;   insn.mnemonic = "dcbi"; break;
        case 371: insn.type = PPCInsnType::MFSPR;  insn.mnemonic = "mftb";
                  insn.spr = PPC_SPR(raw); break; // mftb uses same encoding as mfspr

        // Trap
        case 4:   insn.type = PPCInsnType::TW;     insn.mnemonic = "tw"; break;

        default:
            insn.type = PPCInsnType::UNKNOWN;
            insn.mnemonic = "op31_???";
        }
        break;
    }

    // ---- Opcode 59: Single-precision float ----
    case 59: {
        uint32_t xo5 = (raw >> 1) & 0x1F;
        insn.rd = PPC_FD(raw);
        insn.ra = PPC_FA(raw);
        insn.rb = PPC_FB(raw);
        insn.rc_reg = PPC_FC(raw);
        insn.rc = PPC_RC_FLAG(raw);

        switch (xo5) {
        case 18: insn.type = PPCInsnType::FDIVS;   insn.mnemonic = "fdivs"; break;
        case 20: insn.type = PPCInsnType::FSUBS;   insn.mnemonic = "fsubs"; break;
        case 21: insn.type = PPCInsnType::FADDS;   insn.mnemonic = "fadds"; break;
        case 24: insn.type = PPCInsnType::FRES;    insn.mnemonic = "fres"; break;
        case 25: insn.type = PPCInsnType::FMULS;   insn.mnemonic = "fmuls"; break;
        case 28: insn.type = PPCInsnType::FMSUBS;  insn.mnemonic = "fmsubs"; break;
        case 29: insn.type = PPCInsnType::FMADDS;  insn.mnemonic = "fmadds"; break;
        case 30: insn.type = PPCInsnType::FNMSUBS; insn.mnemonic = "fnmsubs"; break;
        case 31: insn.type = PPCInsnType::FNMADDS; insn.mnemonic = "fnmadds"; break;
        default:
            insn.type = PPCInsnType::UNKNOWN;
            insn.mnemonic = "op59_???";
        }
        break;
    }

    // ---- Opcode 63: Double-precision float ----
    case 63: {
        uint32_t xo = PPC_XO(raw);
        uint32_t xo5 = (raw >> 1) & 0x1F;
        insn.rd = PPC_FD(raw);
        insn.ra = PPC_FA(raw);
        insn.rb = PPC_FB(raw);
        insn.rc_reg = PPC_FC(raw);
        insn.rc = PPC_RC_FLAG(raw);

        // Check 5-bit sub-opcode first (for multiply-type)
        switch (xo5) {
        case 18: insn.type = PPCInsnType::FDIV;    insn.mnemonic = "fdiv"; return insn;
        case 20: insn.type = PPCInsnType::FSUB;    insn.mnemonic = "fsub"; return insn;
        case 21: insn.type = PPCInsnType::FADD;    insn.mnemonic = "fadd"; return insn;
        case 23: insn.type = PPCInsnType::FSEL;    insn.mnemonic = "fsel"; return insn;
        case 25: insn.type = PPCInsnType::FMUL;    insn.mnemonic = "fmul"; return insn;
        case 26: insn.type = PPCInsnType::FRSQRTE; insn.mnemonic = "frsqrte"; return insn;
        case 28: insn.type = PPCInsnType::FMSUB;   insn.mnemonic = "fmsub"; return insn;
        case 29: insn.type = PPCInsnType::FMADD;   insn.mnemonic = "fmadd"; return insn;
        case 30: insn.type = PPCInsnType::FNMSUB;  insn.mnemonic = "fnmsub"; return insn;
        case 31: insn.type = PPCInsnType::FNMADD;  insn.mnemonic = "fnmadd"; return insn;
        default: break;
        }

        // 10-bit sub-opcode
        switch (xo) {
        case 0:   insn.type = PPCInsnType::FCMPU;  insn.mnemonic = "fcmpu";
                  insn.crfd = PPC_CRFD(raw); break;
        case 32:  insn.type = PPCInsnType::FCMPO;  insn.mnemonic = "fcmpo";
                  insn.crfd = PPC_CRFD(raw); break;
        case 72:  insn.type = PPCInsnType::FMR;    insn.mnemonic = "fmr"; break;
        case 40:  insn.type = PPCInsnType::FNEG;   insn.mnemonic = "fneg"; break;
        case 264: insn.type = PPCInsnType::FABS;   insn.mnemonic = "fabs"; break;
        case 136: insn.type = PPCInsnType::FNABS;  insn.mnemonic = "fnabs"; break;
        case 12:  insn.type = PPCInsnType::FRSP;   insn.mnemonic = "frsp"; break;
        case 14:  insn.type = PPCInsnType::FCTIW;  insn.mnemonic = "fctiw"; break;
        case 15:  insn.type = PPCInsnType::FCTIWZ; insn.mnemonic = "fctiwz"; break;
        case 583: insn.type = PPCInsnType::MFFS;   insn.mnemonic = "mffs"; break;
        case 711: insn.type = PPCInsnType::MTFSF;  insn.mnemonic = "mtfsf"; break;
        case 38:  insn.type = PPCInsnType::MTFSF;  insn.mnemonic = "mtfsb1"; break; // set FPSCR bit
        case 70:  insn.type = PPCInsnType::MTFSF;  insn.mnemonic = "mtfsb0"; break; // clear FPSCR bit
        default:
            insn.type = PPCInsnType::UNKNOWN;
            insn.mnemonic = "op63_???";
        }
        break;
    }

    // ---- Trap ----
    case 3: {
        insn.type = PPCInsnType::TWI;
        insn.mnemonic = "twi";
        insn.rd = PPC_RD(raw); // TO field
        insn.ra = PPC_RA(raw);
        insn.simm = PPC_SIMM(raw);
        break;
    }

    // ---- System Call ----
    case 17: {
        insn.type = PPCInsnType::SC;
        insn.mnemonic = "sc";
        break;
    }

    default:
        insn.type = PPCInsnType::UNKNOWN;
        insn.mnemonic = "???";
        break;
    }

    return insn;
}

std::vector<PPCInsn> ppc_disasm_range(const uint8_t* data, uint32_t base_addr, uint32_t size) {
    std::vector<PPCInsn> result;
    result.reserve(size / 4);
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t raw = ((uint32_t)data[i] << 24) | ((uint32_t)data[i+1] << 16) |
                       ((uint32_t)data[i+2] << 8) | data[i+3];
        result.push_back(ppc_disasm(raw, base_addr + i));
    }
    return result;
}

} // namespace ww
