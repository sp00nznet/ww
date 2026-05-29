"""Convert a Nintendo GameCube .dol into a stock ELF that Ghidra can load.

Why this exists: Ghidra ships no built-in DOL loader. Rather than write a
custom Ghidra loader extension, we lay out each DOL section as a PROGBITS
segment in a big-endian PowerPC32 ELF. Ghidra's stock ELF loader does the
rest, and the section addresses match the game's real memory map so the
function/symbol exports come out with the right VAs.

DOL header layout (offset / field):
   0x00   7 x u32   text section file offsets
   0x1C  11 x u32   data section file offsets
   0x48   7 x u32   text section memory addresses
   0x64  11 x u32   data section memory addresses
   0x90   7 x u32   text section sizes
   0xAC  11 x u32   data section sizes
   0xD8       u32   bss memory address
   0xDC       u32   bss size
   0xE0       u32   entry point

Usage:
    python tools/ghidra/dol_to_elf.py main.dol main.elf
"""

import struct
import sys

# Minimal ELF emission. We only need:
#   - 32-bit big-endian PPC
#   - one LOAD program header per non-empty DOL section
#   - executable flag on text segments
EI_NIDENT = 16
ELFCLASS32 = 1
ELFDATA2MSB = 2
EV_CURRENT = 1
ET_EXEC = 2
EM_PPC = 20
PT_LOAD = 1
PF_X = 1
PF_W = 2
PF_R = 4


def parse_dol(blob: bytes):
    """Return list of (vaddr, size, data, is_text) sections."""
    text_offs  = struct.unpack(">7I",  blob[0x00:0x00 + 7 * 4])
    data_offs  = struct.unpack(">11I", blob[0x1C:0x1C + 11 * 4])
    text_addrs = struct.unpack(">7I",  blob[0x48:0x48 + 7 * 4])
    data_addrs = struct.unpack(">11I", blob[0x64:0x64 + 11 * 4])
    text_sizes = struct.unpack(">7I",  blob[0x90:0x90 + 7 * 4])
    data_sizes = struct.unpack(">11I", blob[0xAC:0xAC + 11 * 4])
    bss_addr   = struct.unpack(">I",   blob[0xD8:0xDC])[0]
    bss_size   = struct.unpack(">I",   blob[0xDC:0xE0])[0]
    entry      = struct.unpack(">I",   blob[0xE0:0xE4])[0]

    sections = []
    for off, addr, sz in zip(text_offs, text_addrs, text_sizes):
        if sz and addr:
            sections.append((addr, sz, blob[off:off + sz], True))
    for off, addr, sz in zip(data_offs, data_addrs, data_sizes):
        if sz and addr:
            sections.append((addr, sz, blob[off:off + sz], False))
    sections.sort(key=lambda s: s[0])
    return sections, entry, (bss_addr, bss_size)


def emit_elf(out_path: str, sections, entry, bss):
    """Write a 32-bit big-endian PPC ELF with one PROGBITS LOAD per section."""
    # ELF header: 52 bytes. Program headers start right after: ehsize = 52.
    e_ehsize = 52
    e_phentsize = 32
    # Include BSS as one extra empty LOAD if present.
    bss_addr, bss_size = bss
    have_bss = bss_size > 0
    n_phdrs = len(sections) + (1 if have_bss else 0)
    e_phoff = e_ehsize
    file_data_offset = e_phoff + n_phdrs * e_phentsize

    # Pre-compute file offsets for each section's payload.
    file_offsets = []
    cursor = file_data_offset
    for vaddr, vsize, data, is_text in sections:
        # Align to 0x20 — keeps Ghidra and most tools happy and matches DOL spec.
        if cursor % 0x20:
            cursor += 0x20 - (cursor % 0x20)
        file_offsets.append(cursor)
        cursor += len(data)
    end_of_file = cursor

    with open(out_path, "wb") as fp:
        # ----- ELF header (52 bytes, BE) -----
        e_ident = bytearray(EI_NIDENT)
        e_ident[0:4] = b"\x7fELF"
        e_ident[4] = ELFCLASS32
        e_ident[5] = ELFDATA2MSB
        e_ident[6] = EV_CURRENT
        # Pad rest to zero.
        fp.write(bytes(e_ident))
        fp.write(struct.pack(
            ">HHIIIIIHHHHHH",
            ET_EXEC,                # e_type
            EM_PPC,                 # e_machine
            EV_CURRENT,             # e_version
            entry,                  # e_entry
            e_phoff,                # e_phoff
            0,                      # e_shoff (no section headers)
            0,                      # e_flags
            e_ehsize,               # e_ehsize
            e_phentsize,            # e_phentsize
            n_phdrs,                # e_phnum
            0,                      # e_shentsize
            0,                      # e_shnum
            0,                      # e_shstrndx
        ))
        # ----- Program headers -----
        for (vaddr, vsize, data, is_text), foff in zip(sections, file_offsets):
            flags = PF_R | PF_X if is_text else PF_R | PF_W
            fp.write(struct.pack(
                ">IIIIIIII",
                PT_LOAD,            # p_type
                foff,               # p_offset
                vaddr,              # p_vaddr
                vaddr,              # p_paddr
                len(data),          # p_filesz
                vsize,              # p_memsz
                flags,              # p_flags
                0x20,               # p_align
            ))
        if have_bss:
            fp.write(struct.pack(
                ">IIIIIIII",
                PT_LOAD,
                end_of_file,        # p_offset (unused since p_filesz=0)
                bss_addr,
                bss_addr,
                0,                  # p_filesz
                bss_size,           # p_memsz
                PF_R | PF_W,
                0x20,
            ))
        # ----- Section payloads -----
        for (vaddr, vsize, data, is_text), foff in zip(sections, file_offsets):
            fp.seek(foff)
            fp.write(data)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: dol_to_elf.py <in.dol> <out.elf>\n")
        return 1
    with open(argv[1], "rb") as fp:
        blob = fp.read()
    sections, entry, bss = parse_dol(blob)
    emit_elf(argv[2], sections, entry, bss)
    print("[dol_to_elf] %s -> %s  sections=%d  entry=0x%08X  bss=0x%X+0x%X" %
          (argv[1], argv[2], len(sections), entry, bss[0], bss[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
