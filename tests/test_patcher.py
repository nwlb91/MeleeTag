#!/usr/bin/env python3
"""End-to-end tests of the ISO patcher on synthetic GameCube disc images.

A real Melee ISO cannot be shipped, so these images reproduce the parts the
patcher reads: the disc header, apploader header, a DOL whose sections sit
at Melee's addresses (with plausible first instructions at every patch
site) and a file table with files.  The patcher is told the synthetic DOL's
SHA-1 (--expect-dol-sha1); everything else runs as on a real ISO.

Usage: test_patcher.py <meleetag-patch> <payload.bin> <payload.sites> <workdir>
"""
import hashlib
import os
import struct
import subprocess
import sys

MFLR = 0x7C0802A6
STWU = 0x9421FFE0
LINK = 0x80A00000

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


def be32(b, off):
    return struct.unpack_from(">I", b, off)[0]


def make_dol(sites, pc_relative_site=None):
    """Two text sections covering Melee's code range, one data section."""
    text0 = (0x80003100, 0x2400)
    text1 = (0x800055E0, 0x803B7240 - 0x800055E0)
    data0 = (0x803B7240, 0x1000)
    secs = [text0, text1]
    body = bytearray()
    offs = []
    pos = 0x100
    for addr, size in secs + [data0]:
        offs.append(pos)
        pos += size
    dol = bytearray(pos)
    hdr = bytearray(0x100)
    for i, (addr, size) in enumerate(secs):
        struct.pack_into(">I", hdr, i * 4, offs[i])
        struct.pack_into(">I", hdr, 0x48 + i * 4, addr)
        struct.pack_into(">I", hdr, 0x90 + i * 4, size)
    struct.pack_into(">I", hdr, 7 * 4, offs[2])
    struct.pack_into(">I", hdr, 0x48 + 7 * 4, data0[0])
    struct.pack_into(">I", hdr, 0x90 + 7 * 4, data0[1])
    struct.pack_into(">III", hdr, 0xD8, 0x804316C0, 0xA6000, 0x8000522C)
    dol[0:0x100] = hdr
    # Deterministic filler so the hash is stable.
    for i in range(0x100, len(dol), 4):
        struct.pack_into(">I", dol, i, (i * 2654435761) & 0xFFFFFFFF)
    for site in sites:
        off = offs[1] + (site - text1[0])
        insn = MFLR if site != 0x80375304 else STWU
        if site == pc_relative_site:
            insn = 0x48000011  # bl +0x10
        struct.pack_into(">I", dol, off, insn)
    return bytes(dol)


def make_iso(dol, gap_after_dol, game_id=b"GALE01", version=2,
             app_size=0x1000, tight=False, mid_gap=0):
    """boot.bin/bi2/apploader, DOL, FST, three files.

    tight: files start right after the FST (32-byte aligned) instead of at
    the next 32 KiB boundary.  mid_gap: free bytes after the first file.
    """
    app = bytearray(0x20 + app_size)
    struct.pack_into(">III", app, 0x10, 0x81200000, app_size, 0)
    dol_off = 0x1E800
    fst_off = dol_off + len(dol) + gap_after_dol
    fst_off = (fst_off + 0x7FFF) & ~0x7FFF
    files = [(b"PlFx.dat", os.urandom(0x23456)),
             (b"GrNBa.dat", os.urandom(0x40000)),
             (b"opening.bnr", os.urandom(0x1960))]
    names = b""
    name_offs = []
    for n, _ in files:
        name_offs.append(len(names))
        names += n + b"\0"
    fst_size = 12 * (len(files) + 1) + len(names)
    align = 0x20 if tight else 0x8000
    data_off = (fst_off + fst_size + align - 1) & ~(align - 1)
    fst = bytearray(fst_size)
    struct.pack_into(">III", fst, 0, 0x01000000, 0, len(files) + 1)
    pos = data_off
    placed = []
    for i, (n, content) in enumerate(files):
        struct.pack_into(">III", fst, 12 * (i + 1), name_offs[i], pos, len(content))
        placed.append((pos, content))
        pos = (pos + len(content) + align - 1) & ~(align - 1)
        if i == 0:
            pos += mid_gap
    fst[12 * (len(files) + 1):] = names
    size = pos
    iso = bytearray(size)
    iso[0:6] = game_id
    iso[6:8] = bytes([0, version])
    iso[0x1C:0x20] = b"\xC2\x33\x9F\x3D"
    struct.pack_into(">IIII", iso, 0x420, dol_off, fst_off, fst_size, fst_size)
    iso[0x2440:0x2440 + len(app)] = app
    iso[dol_off:dol_off + len(dol)] = dol
    iso[fst_off:fst_off + fst_size] = fst
    for off, content in placed:
        iso[off:off + len(content)] = content
    return bytes(iso), placed


def run(patcher, args):
    p = subprocess.run([patcher] + args, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def dol_sections(dol):
    out = []
    for i in range(18):
        off, addr, size = be32(dol, i * 4), be32(dol, 0x48 + i * 4), be32(dol, 0x90 + i * 4)
        if size:
            out.append((i, off, addr, size))
    return out


def read_dol(iso):
    off = be32(iso, 0x420)
    end = max(o + s for _, o, _, s in dol_sections(iso[off:off + 0x100]))
    return off, iso[off:off + end]


def addr_word(dol, addr):
    for _, off, a, size in dol_sections(dol):
        if a <= addr < a + size:
            return be32(dol, off + addr - a)
    raise KeyError(hex(addr))


def test_patch(patcher, payload, sites, work, name, iso_args, extra,
               expect_strategy):
    dol = make_dol(sites)
    iso, files = make_iso(dol, **iso_args)
    src = os.path.join(work, f"{name}.iso")
    dst = os.path.join(work, f"{name}.MeleeTag.iso")
    open(src, "wb").write(iso)
    sha = hashlib.sha1(dol).hexdigest()
    args = extra + ["--expect-dol-sha1", sha]
    if expect_strategy == 3 or expect_strategy is None:
        args += ["--max-image-size", str(len(iso))]
    code, out = run(patcher, args + [src])
    if expect_strategy is None:
        check(code != 0 and "no room" in out, f"{name}: must fail: {out}")
        check(not os.path.exists(dst), f"{name}: no output on failure")
        return None
    check(code == 0, f"{name}: exit {code}: {out}")
    if code != 0:
        return None
    check(open(src, "rb").read() == iso, "input ISO must be untouched")
    res = open(dst, "rb").read()
    new_off, new_dol = read_dol(res)
    old_fst = be32(iso, 0x424)
    new_fst = be32(res, 0x424)
    fst_size = be32(iso, 0x428)
    if expect_strategy == 1:
        check(new_off == 0x1E800 and new_fst == old_fst, f"{name}: in place")
    elif expect_strategy == 2:
        check(new_off == 0x1E800 and new_fst != old_fst, f"{name}: FST moved")
        check(res[new_fst:new_fst + fst_size] == iso[old_fst:old_fst + fst_size],
              f"{name}: FST copied intact")
        check(not (new_fst < new_off + len(new_dol) and new_off < new_fst + fst_size),
              f"{name}: FST overlaps the DOL")
    else:
        check(new_off != 0x1E800 and new_fst == old_fst, f"{name}: DOL moved")
    check(res[0x428:0x430] == iso[0x428:0x430], "FST size fields changed")
    check(len(res) <= max(len(iso), 0x57058000), "image grew past a disc")
    # Game files untouched.
    for off, content in files:
        check(res[off:off + len(content)] == content, f"{name}: game file changed")
    # Apploader untouched.
    check(res[0x2440:0x2460] == iso[0x2440:0x2460], "apploader changed")
    # Old sections intact, payload added in a free text slot.
    old = {(a, sz) for _, _, a, sz in dol_sections(dol)}
    new = dol_sections(new_dol)
    payload_secs = [x for x in new if x[2] == LINK]
    check(len(payload_secs) == 1 and payload_secs[0][0] < 7,
          "payload must be one text section at 0x80A00000")
    check(old <= {(a, sz) for _, _, a, sz in new}, "existing DOL sections changed")
    for _, off, a, sz in dol_sections(dol):
        nsec = [x for x in new if x[2] == a][0]
        a_bytes = dol[off:off + sz]
        b_bytes = new_dol[nsec[1]:nsec[1] + sz]
        if a == 0x800055E0:
            # Only the boot site word may differ.
            diff = [i for i in range(0, sz, 4) if a_bytes[i:i + 4] != b_bytes[i:i + 4]]
            check(diff == [0x80375304 - a], f"{name}: text changes {diff[:4]}")
        else:
            check(a_bytes == b_bytes, f"{name}: section {a:#x} changed")
    _, poff, _, psize = payload_secs[0]
    img = new_dol[poff:poff + psize]
    check(psize >= len(payload), "payload section too small")
    # Boot branch.
    entry = be32(img, 0x20)
    bi = addr_word(new_dol, 0x80375304)
    disp = bi & 0x03FFFFFC
    if disp & 0x02000000:
        disp -= 0x04000000
    check(bi >> 26 == 18 and (0x80375304 + disp) & 0xFFFFFFFF == entry,
          "boot site must branch to the payload entry")
    check(be32(img, 0x28) == STWU, "boot_orig must hold HSD_OSInit's first word")
    # Hooks: original first instructions recorded.
    hooks, nh = be32(img, 0x14) - LINK, be32(img, 0x18)
    check(nh == len(sites) - 1, "hook count")
    for i in range(nh):
        site = be32(img, hooks + i * 16)
        check(be32(img, hooks + i * 16 + 12) == addr_word(dol, site),
              f"hook {site:#x} original instruction")
        check(addr_word(new_dol, site) == addr_word(dol, site),
              "hook sites are patched at boot, not on disc")
    # Everything else identical to the built payload.
    diff = [i for i in range(0, len(payload), 4)
            if img[i:i + 4] != payload[i:i + 4]]
    cfg = be32(img, 0x2C) - LINK
    allowed = {0x28, cfg + 4, cfg + 8, cfg + 12} | {hooks + i * 16 + 12 for i in range(nh)}
    check(set(diff) <= allowed, f"unexpected payload changes at {[hex(d) for d in diff]}")
    return img, cfg


def main():
    patcher, payload_path, sites_path, work = sys.argv[1:5]
    os.makedirs(work, exist_ok=True)
    payload = open(payload_path, "rb").read()
    sites = [int(l.split()[0], 16) for l in open(sites_path) if l.strip()]

    # 1. Room after the DOL: rewritten in place; options applied.
    r = test_patch(patcher, payload, sites, work, "inplace",
                   dict(gap_after_dol=0x20000),
                   ["--cooldown", "45", "--no-hud", "--max-fighters", "10"], 1)
    if r:
        img, cfg = r
        check(be32(img, cfg + 4) == 45, "cooldown option")
        check(be32(img, cfg + 8) == 2, "--no-hud")
        check(be32(img, cfg + 12) == 10, "fighter cap option")

    # 2. The file table right after the DOL (the retail layout): the DOL
    #    stays, the small file table moves.
    r = test_patch(patcher, payload, sites, work, "fstmove",
                   dict(gap_after_dol=0), [], 2)
    if r:
        img, cfg = r
        check(be32(img, cfg + 4) == 30, "default cooldown")
        check(be32(img, cfg + 8) == 3, "default flags: HUD + netplay sync")
        check(be32(img, cfg + 12) == 8, "default fighter cap")

    # 3. Files packed right behind the table, no slack at the end, but a
    #    large hole between files: the DOL moves into the hole.
    test_patch(patcher, payload, sites, work, "dolmove",
               dict(gap_after_dol=0, tight=True, app_size=0x1B000,
                    mid_gap=0x800000), [], 3)

    # 4. Nowhere to go: refused, nothing written.
    test_patch(patcher, payload, sites, work, "full",
               dict(gap_after_dol=0, tight=True, app_size=0x1B000), [], None)

    # 3. Patching a patched ISO is refused.
    dst = os.path.join(work, "fstmove.MeleeTag.iso")
    if os.path.exists(dst):
        code, out = run(patcher, [dst, os.path.join(work, "twice.iso")])
        check(code != 0 and "already" in out, f"double patch: {out}")

    # 4. Wrong game / revision.
    dol = make_dol(sites)
    for gid, ver, name in ((b"GALP01", 2, "pal"), (b"GALE01", 1, "rev1")):
        iso, _ = make_iso(dol, 0, gid, ver)
        p = os.path.join(work, f"{name}.iso")
        open(p, "wb").write(iso)
        code, out = run(patcher, ["--expect-dol-sha1", hashlib.sha1(dol).hexdigest(), p])
        check(code != 0 and "1.02" in out, f"{name} must be refused: {out}")

    # 5. A modified executable is refused (real hash expected).
    iso, _ = make_iso(dol, 0)
    p = os.path.join(work, "modded.iso")
    open(p, "wb").write(iso)
    code, out = run(patcher, [p])
    check(code != 0 and "retail" in out, f"hash mismatch must be refused: {out}")
    check(not os.path.exists(os.path.join(work, "modded.MeleeTag.iso")),
          "no output on refusal")

    # 6. A PC-relative first instruction at a hook site is refused.
    bad = make_dol(sites, pc_relative_site=sites[1])
    iso, _ = make_iso(bad, 0)
    p = os.path.join(work, "pcrel.iso")
    open(p, "wb").write(iso)
    code, out = run(patcher, ["--expect-dol-sha1", hashlib.sha1(bad).hexdigest(), p])
    check(code != 0 and "cannot be moved" in out, f"pc-relative site: {out}")

    # 7. --verify writes nothing.
    p = os.path.join(work, "verify.iso")
    open(p, "wb").write(make_iso(dol, 0)[0])
    code, out = run(patcher, ["--verify", "--expect-dol-sha1",
                              hashlib.sha1(dol).hexdigest(), p])
    check(code == 0 and not os.path.exists(os.path.join(work, "verify.MeleeTag.iso")),
          f"--verify: {out}")

    # 8. Bad options.
    code, _ = run(patcher, ["--cooldown", "9999", p])
    check(code == 2, "out-of-range cooldown rejected")

    print(f"test_patcher: {'FAILED' if failures else 'ok'} ({failures} failures)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
