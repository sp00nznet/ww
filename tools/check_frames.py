#!/usr/bin/env python3
"""
Find recompiled functions that pop a stack frame they never pushed.

gcrecomp's CFG builder promotes some mid-function branch targets to standalone
functions. A shared epilogue turned into a "function" ends with

    r1 = r1 + N;  blr

but has no matching `stwu r1, -N(r1)`. Called through the indirect dispatch
table, it pops a frame nobody pushed: r1 leaks N bytes per call, and any
callee-saved register it "restores" comes back as whatever happened to be at
that stack slot. The caller then keeps running with a corrupted r31, which is
how a sane loop bound turns into a runaway one.

The three-way duplicates this reports (same length, same pop size, addresses a
few instructions apart) are the clearest tell: one real function discovered at
several entry points.

Usage:
    python tools/check_frames.py [recompiled_dir]
"""

import glob
import os
import re
import sys
from collections import defaultdict

FUNC = re.compile(
    r"void (func_[0-9A-F]{8})\(PPCContext\* ctx, Memory\* mem\) \{(.*?)\r?\n\}\r?\n",
    re.S,
)
PUSH = "MEM_WRITE32(ea, ctx->r[1])"
POP = re.compile(r"ctx->r\[1\] = \(int32_t\)ctx->r\[1\] \+ (\d+)")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "recompiled"
    files = sorted(glob.glob(os.path.join(root, "*.cpp")))
    if not files:
        print("no .cpp files under %s" % root, file=sys.stderr)
        return 2

    total = 0
    bad = []
    for path in files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            src = fh.read()
        for m in FUNC.finditer(src):
            name, body = m.group(1), m.group(2)
            total += 1
            pops = POP.findall(body)
            if pops and PUSH not in body:
                bad.append((name, os.path.basename(path), int(pops[0]),
                            len(body.split("\n"))))

    print("recompiled functions : %d" % total)
    print("pop without push     : %d (%.1f%%)"
          % (len(bad), 100.0 * len(bad) / total if total else 0.0))

    # Same length + same pop size means one function found at several entries.
    groups = defaultdict(list)
    for name, _f, pop, lines in bad:
        groups[(pop, lines)].append(name)
    dupes = {k: v for k, v in groups.items() if len(v) > 1}
    print("likely duplicate entries into one function: %d groups, %d functions"
          % (len(dupes), sum(len(v) for v in dupes.values())))

    if bad:
        print("\nworst offenders (largest frame popped):")
        for name, f, pop, lines in sorted(bad, key=lambda x: -x[2])[:15]:
            print("  %s  pops %-4d lines=%-5d %s" % (name, pop, lines, f))

    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
