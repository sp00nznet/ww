#!/usr/bin/env python3
"""
Preserve hand-applied edits to generated code across a recompile.

recompiled/ is generated output, but a number of functions in it are edited by
hand: hardware waits that would spin forever (VIWaitForRetrace, GX draw-done),
allocators redirected to the host bump arena, DVD reads routed to the ISO
reader, the assert handler defanged. Regenerating throws all of that away, and
until now there was no way to get it back except by redoing it from memory.

    python tools/patches.py extract     recompiled/  -> tools/patches/
    python tools/patches.py apply       tools/patches/ -> recompiled/
    python tools/patches.py check       report drift without writing anything

A patch is keyed by GameCube address, not by file or by name: the recompiler
packs functions into recomp_NNNN.cpp by discovery order, so a function moves
between files whenever anything upstream changes. Each patch holds the whole
replacement block, from the generated `// ---- name @ 0xADDR ----` banner
through the closing brace.

apply fails loudly, and non-zero, if a patch has nowhere to go. That case is
not hypothetical: the CFG entry-promotion fix legitimately deletes thousands of
bogus function entries, and if one of them was patched we need to know rather
than quietly lose the edit.
"""

import glob
import os
import re
import sys

PATCH_DIR = os.path.join("tools", "patches")
MARKER = "// PATCHED"

# The generated banner that precedes every function. Name varies with the
# symbol map in use, so key on the address.
BANNER = re.compile(r"^// ---- (\S+) @ 0x([0-9A-Fa-f]{8}) ----\s*$")


def _blocks(path):
    """Yield (addr, name, start, end) for each function block in a file.

    `end` is the index just past the line that is exactly `}` — generated code
    always closes a function at column zero, while every line of a body is
    indented, so that is an unambiguous terminator.
    """
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    i = 0
    while i < len(lines):
        m = BANNER.match(lines[i])
        if m:
            start = i
            j = i + 1
            while j < len(lines) and lines[j] != "}":
                j += 1
            if j < len(lines):
                yield int(m.group(2), 16), m.group(1), start, j + 1, lines
                i = j
        i += 1


def extract(src):
    os.makedirs(PATCH_DIR, exist_ok=True)
    found = 0
    for path in sorted(glob.glob(os.path.join(src, "*.cpp"))):
        for addr, name, start, end, lines in _blocks(path):
            block = lines[start:end]
            if not any(MARKER in l for l in block):
                continue
            out = os.path.join(PATCH_DIR, "func_%08X.patch" % addr)
            with open(out, "w", encoding="utf-8", newline="\n") as fh:
                fh.write("# addr=0x%08X name=%s from=%s\n"
                         % (addr, name, os.path.basename(path)))
                fh.write("\n".join(block) + "\n")
            found += 1
            print("  extracted 0x%08X (%s) from %s" % (addr, name, os.path.basename(path)))
    print("extracted %d patch(es) to %s" % (found, PATCH_DIR))
    return 0 if found else 1


def _load_patches():
    out = {}
    for p in sorted(glob.glob(os.path.join(PATCH_DIR, "*.patch"))):
        with open(p, encoding="utf-8") as fh:
            text = fh.read().split("\n")
        m = re.match(r"# addr=0x([0-9A-Fa-f]{8})", text[0])
        if not m:
            print("  %s: missing addr header, skipping" % p, file=sys.stderr)
            continue
        body = text[1:]
        while body and body[-1] == "":
            body.pop()
        out[int(m.group(1), 16)] = (p, body)
    return out


def apply(dst, dry_run=False):
    patches = _load_patches()
    if not patches:
        print("no patches in %s" % PATCH_DIR, file=sys.stderr)
        return 1

    applied, unchanged = {}, 0
    for path in sorted(glob.glob(os.path.join(dst, "*.cpp"))):
        blocks = list(_blocks(path))
        if not blocks:
            continue
        lines = blocks[0][4]
        edits = []
        for addr, _name, start, end, _l in blocks:
            if addr not in patches or addr in applied:
                continue
            _src, body = patches[addr]
            if lines[start:end] == body:
                unchanged += 1
                applied[addr] = path
                continue
            edits.append((start, end, body))
            applied[addr] = path
        if not edits or dry_run:
            continue
        for start, end, body in sorted(edits, reverse=True):
            lines[start:end] = body
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(lines))

    missing = sorted(set(patches) - set(applied))
    verb = "would apply" if dry_run else "applied"
    print("%s %d patch(es), %d already current, %d with no target"
          % (verb, len(applied) - unchanged, unchanged, len(missing)))
    for addr in missing:
        print("  MISSING 0x%08X (%s) -- function not in generated output"
              % (addr, os.path.basename(patches[addr][0])), file=sys.stderr)
    return 1 if missing else 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    where = sys.argv[2] if len(sys.argv) > 2 else "recompiled"
    if cmd == "extract":
        return extract(where)
    if cmd == "apply":
        return apply(where)
    if cmd == "check":
        return apply(where, dry_run=True)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
