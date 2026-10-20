#!/usr/bin/env python3
"""Turn the linked MeleeTag ELF into the payload image the patcher embeds.

The ELF must be linked with --emit-relocs (-q) so its relocations survive.
For every relocation in the .payload section this tool decides whether the
boot relocator must touch it when the image moves from TAG_LINK_BASE to the
top of the arena:

* absolute references (ADDR32/16) to addresses inside the image move with it;
* absolute references to engine addresses stay valid and are skipped;
* relative branches inside the image stay valid and are skipped;
* relative branches to engine code must be re-aimed (REL24 entries).

Outputs:
  <out>.bin    image followed by the relocation table (big-endian)
  <out>.h      the same bytes as a C array for the patcher
  <out>.sites  "ADDRESS SIZE NAME" lines for every engine word the mod patches

Usage: mkpayload.py <payload.elf> <out-prefix>
"""

import hashlib
import struct
import sys

LINK_BASE = 0x80A00000
TAG_PAYLOAD_MAGIC = 0x4D544147
BOOT_SITE = 0x80375304

R_PPC_NONE = 0
R_PPC_ADDR32 = 1
R_PPC_ADDR16_LO = 4
R_PPC_ADDR16_HI = 5
R_PPC_ADDR16_HA = 6
R_PPC_REL24 = 10
R_PPC_REL14 = 11
R_PPC_REL32 = 26

TAG_REL = {"ADDR32": 1, "LO": 2, "HA": 3, "HI": 4, "REL24": 5}

HEADER_FMT = ">16I"  # TagPayloadHeader, 0x40 bytes
HOOK_FMT = ">4I"     # TagHook


class Elf:
    def __init__(self, data: bytes):
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
            raise SystemExit("expected a 32-bit big-endian ELF")
        self.data = data
        (_, _, _, _, _, self.shoff, _, _, _, _, self.shentsize, self.shnum,
         self.shstrndx) = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
        self.sections = []
        for i in range(self.shnum):
            fields = struct.unpack_from(">10I", data, self.shoff + i * self.shentsize)
            self.sections.append(dict(zip(
                ("name", "type", "flags", "addr", "offset", "size", "link",
                 "info", "addralign", "entsize"), fields)))
        strtab = self.sections[self.shstrndx]
        for s in self.sections:
            s["sname"] = self._str(strtab, s["name"])

    def _str(self, strtab, off):
        start = strtab["offset"] + off
        end = self.data.index(b"\0", start)
        return self.data[start:end].decode()

    def section(self, name):
        for s in self.sections:
            if s["sname"] == name:
                return s
        return None

    def symbols(self):
        symtab = next(s for s in self.sections if s["type"] == 2)
        strtab = self.sections[symtab["link"]]
        out = []
        for i in range(symtab["size"] // 16):
            name, value, size, info, other, shndx = struct.unpack_from(
                ">IIIBBH", self.data, symtab["offset"] + i * 16)
            out.append({"name": self._str(strtab, name), "value": value,
                        "size": size, "shndx": shndx})
        return out, symtab

    def relocs(self, target):
        syms, symtab_hdr = self.symbols()
        idx = self.sections.index(target)
        out = []
        for s in self.sections:
            if s["type"] != 4 or s["info"] != idx:  # SHT_RELA for target
                continue
            for i in range(s["size"] // 12):
                off, info, addend = struct.unpack_from(">IIi", self.data, s["offset"] + i * 12)
                sym = syms[info >> 8]
                out.append((off, info & 0xFF, sym, addend))
        return out


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    elf = Elf(open(sys.argv[1], "rb").read())
    prefix = sys.argv[2]
    sec = elf.section(".payload")
    if sec is None or sec["addr"] != LINK_BASE:
        raise SystemExit(".payload section missing or not at TAG_LINK_BASE")
    image = bytearray(elf.data[sec["offset"]:sec["offset"] + sec["size"]])
    if len(image) % 32:
        image += bytes(32 - len(image) % 32)
    lo, hi = LINK_BASE, LINK_BASE + len(image)

    def inside(addr):
        return lo <= addr < hi

    entries = []
    for off, rtype, sym, addend in elf.relocs(sec):
        target = (sym["value"] + addend) & 0xFFFFFFFF
        where = off - LINK_BASE
        if rtype == R_PPC_NONE:
            continue
        if rtype in (R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA):
            if not inside(target):
                continue  # engine address, already final
            kind = {R_PPC_ADDR32: "ADDR32", R_PPC_ADDR16_LO: "LO",
                    R_PPC_ADDR16_HI: "HI", R_PPC_ADDR16_HA: "HA"}[rtype]
            entries.append((TAG_REL[kind], where, target))
        elif rtype == R_PPC_REL24:
            if inside(target):
                continue  # moves with the branch
            entries.append((TAG_REL["REL24"], where, target))
        elif rtype in (R_PPC_REL14, R_PPC_REL32):
            if not inside(target):
                raise SystemExit(f"relative reloc type {rtype} at {off:08X} leaves the image")
        else:
            raise SystemExit(f"unsupported relocation type {rtype} at {off:08X} ({sym['name']})")

    reloc_blob = b"".join(struct.pack(">II", (k << 24) | w, v) for k, w, v in entries)

    hdr = list(struct.unpack_from(HEADER_FMT, image, 0))
    if hdr[0] != TAG_PAYLOAD_MAGIC:
        raise SystemExit("payload header missing at TAG_LINK_BASE")
    hdr[2] = len(image)        # image_size
    hdr[3] = len(image)        # reloc_off (table follows the image)
    hdr[4] = len(entries)      # reloc_count
    digest = hashlib.sha1(bytes(image)).digest()
    hdr[12] = struct.unpack(">I", digest[:4])[0]  # build_id
    struct.pack_into(HEADER_FMT, image, 0, *hdr)

    hooks_addr, hook_count = hdr[5], hdr[6]
    sites = [(BOOT_SITE, 4, "boot:HSD_OSInit")]
    syms, _ = elf.symbols()
    by_addr = {}
    for s in syms:
        if s["name"] and s["shndx"] == 0xFFF1:  # absolute engine symbols
            by_addr.setdefault(s["value"], s["name"])
    for i in range(hook_count):
        site, handler, tramp, orig = struct.unpack_from(
            HOOK_FMT, image, hooks_addr - LINK_BASE + i * 16)
        if inside(site) or not (0x80003100 <= site < 0x803B7240):
            raise SystemExit(f"hook {i} site {site:08X} is not engine code")
        sites.append((site, 4, "hook:" + by_addr.get(site, f"{site:08X}")))

    payload = bytes(image) + reloc_blob
    with open(prefix + ".bin", "wb") as f:
        f.write(payload)
    with open(prefix + ".h", "w", encoding="utf-8") as f:
        f.write("/* Generated by tools/mkpayload.py; do not edit. */\n")
        f.write(f"#define TAG_PAYLOAD_SIZE {len(payload)}u\n")
        f.write(f"#define TAG_PAYLOAD_BUILD_ID 0x{hdr[12]:08X}u\n")
        f.write("static const unsigned char tag_payload_bin[] = {\n")
        for i in range(0, len(payload), 16):
            f.write("  " + ", ".join(f"0x{b:02X}" for b in payload[i:i + 16]) + ",\n")
        f.write("};\n")
    with open(prefix + ".sites", "w", encoding="utf-8") as f:
        for addr, size, name in sites:
            f.write(f"{addr:08X} {size} {name}\n")
    print(f"payload: image {len(image)} bytes, {len(entries)} relocations, "
          f"{hook_count} hooks, build {hdr[12]:08X}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
