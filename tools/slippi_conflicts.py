#!/usr/bin/env python3
"""Check MeleeTag's patch sites against every address Slippi's codes modify.

Slippi Dolphin applies its Gecko codeset (GALE01r2.ini) on boot.  If one of
those codes writes to an instruction MeleeTag also patches, whichever writes
last wins and the other mod silently breaks.  This tool parses the Gecko code
lists shipped in slippi-ssbm-asm (netplay + playback, every code group,
including optional ones) and fails if any MeleeTag patch range overlaps a
Slippi write range.

Usage: slippi_conflicts.py <patch_sites.txt> <GALE01r2.ini> [more .ini ...]

patch_sites.txt contains one "ADDRESS SIZE NAME" triple per line (hex
address, decimal size in bytes).
"""

import re
import sys

# Code lines may carry a trailing "#comment" (gecko's source annotations).
CODE_LINE = re.compile(r"^([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*(?:#.*)?$")


def gecko_writes(lines):
    """Yield (address, size, code_name) for each memory write in a codeset.

    Implements the Gecko code types that occur in Slippi's codesets and skips
    the payload lines of every multi-line type so data is never misread as a
    code.  Unknown types raise, so a parser gap cannot hide a conflict.
    """
    code_name = "?"
    words = []
    for raw in lines:
        s = raw.strip()
        if s.startswith("$"):
            words.append(("name", s))
            continue
        m = CODE_LINE.match(s)
        if m:
            words.append(("code", int(m.group(1), 16), int(m.group(2), 16)))
    i = 0
    while i < len(words):
        w = words[i]
        if w[0] == "name":
            code_name = w[1]
            i += 1
            continue
        _, a, b = w
        ctype = (a >> 24) & 0xFE  # low bit selects pointer/base, irrelevant here
        addr = 0x80000000 | (a & 0x01FFFFFF)
        i += 1
        if ctype in (0x00,):
            count = (b >> 16) + 1
            yield addr, count, code_name
        elif ctype == 0x02:
            count = (b >> 16) + 1
            yield addr, 2 * count, code_name
        elif ctype == 0x04:
            yield addr, 4, code_name
        elif ctype == 0x06:
            length = b
            yield addr, length, code_name
            i += (length + 7) // 8
        elif ctype == 0x08:
            # serial write: next line holds count/step; conservatively cover span
            _, c, d = words[i]
            count = ((c >> 16) & 0xFFF) + 1
            step = c & 0xFFFF
            size = {0: 1, 1: 2, 2: 4}.get((c >> 28) & 3, 4)
            yield addr, step * (count - 1) + size, code_name
            i += 1
        elif ctype in (0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E):
            pass  # conditionals, no write
        elif 0x40 <= ctype <= 0x4E or 0x60 <= ctype <= 0x6E:
            pass  # base/pointer/repeat ops
        elif 0x80 <= ctype <= 0x8E:
            if ctype in (0x84, 0x86, 0x8A, 0x8C):
                # gecko-register store/copy ops: address is runtime-dependent
                pass
        elif ctype in (0xA0, 0xA2, 0xA4, 0xA6, 0xA8, 0xAA, 0xAC, 0xAE):
            pass
        elif ctype == 0xC0:
            i += b  # execute ASM
        elif ctype == 0xC2:
            yield addr, 4, code_name  # branch written at addr, code elsewhere
            i += b
        elif ctype == 0xC6:
            yield addr, 4, code_name
        elif ctype in (0xCC, 0xCE):
            pass
        elif ctype in (0xE0, 0xE2, 0xF0):
            pass
        elif ctype in (0xF2, 0xF4):
            i += b & 0xFF
        elif ctype == 0xF6:
            i += b & 0xFF
        else:
            raise ValueError(f"unhandled gecko code type 0x{ctype:02X} in {code_name}")


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    sites = []
    with open(sys.argv[1], encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            addr, size, name = line.split(None, 2)
            sites.append((int(addr, 16), int(size), name))
    writes = []
    for path in sys.argv[2:]:
        if path.endswith(".gct"):
            # Binary GCT: 8-byte header (00D0C0DE x2) then 8-byte code lines.
            with open(path, "rb") as f:
                blob = f.read()
            lines = [f"{blob[o:o+4].hex()} {blob[o+4:o+8].hex()}"
                     for o in range(8, len(blob) - 7, 8)]
            src = lines
        else:
            with open(path, encoding="utf-8", errors="replace") as f:
                src = f.readlines()
        writes.extend((a, n, f"{path}:{c}") for a, n, c in gecko_writes(src))
    conflicts = 0
    for s_addr, s_size, s_name in sites:
        for w_addr, w_size, w_name in writes:
            if s_addr < w_addr + w_size and w_addr < s_addr + s_size:
                print(f"CONFLICT: {s_name} @ {s_addr:08X}+{s_size} overlaps "
                      f"{w_name} @ {w_addr:08X}+{w_size}")
                conflicts += 1
    print(f"checked {len(sites)} MeleeTag patch sites against {len(writes)} "
          f"Slippi writes: {conflicts} conflict(s)")
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
