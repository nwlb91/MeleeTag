/*
 * MeleeTag patcher: vanilla NTSC 1.02 Melee ISO in, MeleeTag ISO out.
 *
 *   meleetag-patch [options] <melee.iso> [output.iso]
 *
 *   --cooldown N        switch cooldown in frames (default 30, 0..600)
 *   --no-hud            no in-match slot display
 *   --max-fighters N    fighter objects alive at once, all players (4..12)
 *   --no-netplay-sync   never enable tag in Slippi online matches
 *   --verify            check the input only, write nothing
 *
 * Without an output path the result is written next to the input as
 * "<name>.MeleeTag.iso".  The input is never modified.
 *
 * What it changes: one new DOL text section with the payload (loaded at
 * 0x80A00000, moved to the top of the arena at boot), one branch over the
 * first instruction of HSD_OSInit, and the DOL's location in the disc
 * header if the bigger DOL has to move.  Every hook's original first
 * instruction is read from this DOL and stored in the payload.
 */
#include "iso.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "dol.h"
#include "payload.h"
#include "sha1.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

/* Paths arrive as UTF-16 on Windows; keep them UTF-8 internally. */
static FILE* xfopen(const char* path, const char* mode)
{
    wchar_t wpath[4096], wmode[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096) ||
        !MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8))
    {
        return NULL;
    }
    return _wfopen(wpath, wmode);
}

static void xremove(const char* path)
{
    wchar_t wpath[4096];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 4096)) {
        _wremove(wpath);
    }
}

static char** utf8_argv(int* argc)
{
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), argc);
    char** argv;
    int i;

    if (wargv == NULL) {
        return NULL;
    }
    argv = calloc((size_t) *argc + 1, sizeof(char*));
    for (i = 0; argv != NULL && i < *argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL,
                                    NULL);
        argv[i] = malloc((size_t) n);
        if (argv[i] != NULL) {
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i], n, NULL,
                                NULL);
        }
    }
    LocalFree(wargv);
    return argv;
}
#else
#define xfopen fopen
#define xremove remove
#endif

#define RETAIL_DOL_SHA1 "08e0bf20134dfcb260699671004527b2d6bb1a45"

#define LINK_BASE 0x80A00000u
#define PAYLOAD_MAGIC 0x4D544147u
#define PAYLOAD_VERSION 1u
#define CONFIG_MAGIC 0x4D434647u
#define HDR_HOOKS 5
#define HDR_HOOK_COUNT 6
#define HDR_BOOT_ENTRY 8
#define HDR_BOOT_SITE 9
#define HDR_BOOT_ORIG 10
#define HDR_CONFIG 11

typedef struct Options {
    const char* in;
    const char* out;
    long cooldown;
    long max_fighters;
    int hud;
    int netsync;
    int verify;
    const char* expect_sha1; /* tests only */
    unsigned long long max_size; /* tests only */
} Options;

static void usage(void)
{
    fprintf(stderr,
            "usage: meleetag-patch [--cooldown N] [--no-hud] "
            "[--max-fighters N]\n"
            "                      [--no-netplay-sync] [--verify] "
            "<melee.iso> [output.iso]\n");
}

static int fail(const char* msg)
{
    fprintf(stderr, "error: %s\n", msg);
    return 1;
}

static int parse_num(const char* s, long lo, long hi, long* out)
{
    char* end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_args(int argc, char** argv, Options* o)
{
    int i;

    memset(o, 0, sizeof(*o));
    o->cooldown = 30;
    o->max_fighters = 8;
    o->hud = 1;
    o->netsync = 1;
    o->max_size = GCM_MAX_SIZE;
    for (i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--cooldown") == 0 && i + 1 < argc) {
            if (parse_num(argv[++i], 0, 600, &o->cooldown) != 0) {
                return -1;
            }
        } else if (strcmp(a, "--max-fighters") == 0 && i + 1 < argc) {
            if (parse_num(argv[++i], 4, 12, &o->max_fighters) != 0) {
                return -1;
            }
        } else if (strcmp(a, "--no-hud") == 0) {
            o->hud = 0;
        } else if (strcmp(a, "--no-netplay-sync") == 0) {
            o->netsync = 0;
        } else if (strcmp(a, "--verify") == 0) {
            o->verify = 1;
        } else if (strcmp(a, "--expect-dol-sha1") == 0 && i + 1 < argc) {
            o->expect_sha1 = argv[++i];
        } else if (strcmp(a, "--max-image-size") == 0 && i + 1 < argc) {
            o->max_size = strtoull(argv[++i], NULL, 0);
        } else if (a[0] == '-' && a[1] == '-') {
            return -1;
        } else if (o->in == NULL) {
            o->in = a;
        } else if (o->out == NULL) {
            o->out = a;
        } else {
            return -1;
        }
    }
    return o->in != NULL ? 0 : -1;
}

static char* default_output(const char* in)
{
    size_t n = strlen(in);
    const char* dot = strrchr(in, '.');
    const char* sep1 = strrchr(in, '/');
    const char* sep2 = strrchr(in, '\\');
    const char* sep = sep1 > sep2 ? sep1 : sep2;
    char* out = malloc(n + 16);

    if (out == NULL) {
        return NULL;
    }
    if (dot == NULL || (sep != NULL && dot < sep)) {
        dot = in + n;
    }
    memcpy(out, in, (size_t) (dot - in));
    strcpy(out + (dot - in), ".MeleeTag.iso");
    return out;
}

static int copy_file(const char* from, const char* to)
{
    FILE* a = xfopen(from, "rb");
    FILE* b;
    static uint8_t buf[1 << 20];
    size_t n;
    int ok = 1;

    if (a == NULL) {
        return -1;
    }
    b = xfopen(to, "wb");
    if (b == NULL) {
        fclose(a);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), a)) > 0) {
        if (fwrite(buf, 1, n, b) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(a)) {
        ok = 0;
    }
    fclose(a);
    if (fclose(b) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}

static int is_pc_relative(uint32_t insn)
{
    uint32_t op = insn >> 26;
    /* b/bl and bc with AA=0 encode a displacement from their own address. */
    return (op == 18 || op == 16) && !(insn & 2);
}

static uint32_t branch(uint32_t from, uint32_t to)
{
    return 0x48000000u | ((to - from) & 0x03FFFFFCu);
}

/* Fill the payload's patch-time fields from this DOL. */
static int prepare_payload(const Dol* dol, uint8_t* img, const Options* o,
                           char* err, size_t errlen)
{
    uint32_t hooks, count, i, cfg;
    uint32_t boot_site = be32(img + HDR_BOOT_SITE * 4);
    size_t off;

    if (be32(img) != PAYLOAD_MAGIC || be32(img + 4) != PAYLOAD_VERSION) {
        snprintf(err, errlen, "embedded payload is damaged");
        return -1;
    }
    off = dol_offset_of(dol, boot_site, 4);
    if (off == 0) {
        snprintf(err, errlen, "boot site %08X not in the DOL", boot_site);
        return -1;
    }
    put_be32(img + HDR_BOOT_ORIG * 4, be32(dol->data + off));

    hooks = be32(img + HDR_HOOKS * 4) - LINK_BASE;
    count = be32(img + HDR_HOOK_COUNT * 4);
    if (hooks + count * 16 > TAG_PAYLOAD_SIZE) {
        snprintf(err, errlen, "embedded payload hook table is damaged");
        return -1;
    }
    for (i = 0; i < count; i++) {
        uint8_t* h = img + hooks + i * 16;
        uint32_t site = be32(h);
        uint32_t insn;
        off = dol_offset_of(dol, site, 4);
        if (off == 0) {
            snprintf(err, errlen, "hook site %08X not in the DOL", site);
            return -1;
        }
        insn = be32(dol->data + off);
        if (insn == 0 || is_pc_relative(insn)) {
            snprintf(err, errlen,
                     "hook site %08X starts with %08X, which cannot be moved",
                     site, insn);
            return -1;
        }
        put_be32(h + 12, insn);
    }

    cfg = be32(img + HDR_CONFIG * 4) - LINK_BASE;
    if (cfg + 16 > TAG_PAYLOAD_SIZE || be32(img + cfg) != CONFIG_MAGIC) {
        snprintf(err, errlen, "embedded payload config is damaged");
        return -1;
    }
    put_be32(img + cfg + 4, (uint32_t) o->cooldown);
    put_be32(img + cfg + 8, (o->hud ? 1u : 0u) | (o->netsync ? 2u : 0u));
    put_be32(img + cfg + 12, (uint32_t) o->max_fighters);
    return 0;
}

int main(int argc, char** argv)
{
    Options o;
    char err[256];
    FILE* f;
    Iso iso;
    Dol dol;
    uint8_t* dolbuf;
    uint8_t* img;
    size_t dolsize, off;
    uint8_t digest[20];
    char hex[41];
    Sha1 sha;
    uint32_t boot_entry, boot_site;
    uint64_t where;
    char* out;

#ifdef _WIN32
    {
        char** u = utf8_argv(&argc);
        if (u != NULL) {
            argv = u;
        }
    }
#endif
    if (parse_args(argc, argv, &o) != 0) {
        usage();
        return 2;
    }

    f = xfopen(o.in, "rb");
    if (f == NULL) {
        fprintf(stderr, "error: cannot open %s: %s\n", o.in, strerror(errno));
        return 1;
    }
    if (iso_open(&iso, f, err, sizeof(err)) != 0) {
        fclose(f);
        return fail(err);
    }
    if (memcmp(iso.header, "GALE01", 6) != 0 || iso.header[7] != 2) {
        fclose(f);
        return fail("this is not Super Smash Bros. Melee NTSC 1.02 (GALE01, "
                    "revision 2)");
    }

    /* The DOL, checked against the retail executable. */
    {
        uint8_t hdr[DOL_HEADER];
        if (iso_read_at(f, iso.dol_off, hdr, sizeof(hdr)) != 0) {
            fclose(f);
            return fail("cannot read the DOL");
        }
        dolsize = dol_extent(hdr);
    }
    dolbuf = malloc(dolsize);
    if (dolbuf == NULL || iso_read_at(f, iso.dol_off, dolbuf, dolsize) != 0) {
        fclose(f);
        return fail("cannot read the DOL");
    }
    fclose(f);
    sha1_init(&sha);
    sha1_update(&sha, dolbuf, dolsize);
    sha1_final(&sha, digest);
    sha1_hex(digest, hex);
    if (dol_parse(&dol, dolbuf, dolsize, err, sizeof(err)) != 0) {
        return fail(err);
    }
    if (dol_offset_of(&dol, LINK_BASE, 4) != 0) {
        return fail("this ISO already has MeleeTag; use a vanilla ISO");
    }
    if (strcmp(hex, o.expect_sha1 ? o.expect_sha1 : RETAIL_DOL_SHA1) != 0) {
        fprintf(stderr,
                "error: the game executable is not the retail NTSC 1.02 one\n"
                "       (sha1 %s, expected %s).\n"
                "       Use an unmodified ISO.\n",
                hex, RETAIL_DOL_SHA1);
        return 1;
    }

    img = malloc(TAG_PAYLOAD_SIZE);
    if (img == NULL) {
        return fail("out of memory");
    }
    memcpy(img, tag_payload_bin, TAG_PAYLOAD_SIZE);
    if (prepare_payload(&dol, img, &o, err, sizeof(err)) != 0) {
        return fail(err);
    }
    if (o.verify) {
        printf("OK: vanilla NTSC 1.02 Melee, every patch site matches.\n");
        return 0;
    }

    /* New DOL: payload section + boot branch. */
    boot_entry = be32(img + HDR_BOOT_ENTRY * 4);
    boot_site = be32(img + HDR_BOOT_SITE * 4);
    if (dol_add_text(&dol, LINK_BASE, img, TAG_PAYLOAD_SIZE, err,
                     sizeof(err)) != 0)
    {
        return fail(err);
    }
    off = dol_offset_of(&dol, boot_site, 4);
    put_be32(dol.data + off, branch(boot_site, boot_entry));

    /* Copy the image, then write the DOL where it fits. */
    out = o.out ? (char*) o.out : default_output(o.in);
    if (out == NULL) {
        return fail("out of memory");
    }
    if (strcmp(out, o.in) == 0) {
        return fail("output must differ from the input");
    }
    printf("Writing %s ...\n", out);
    if (copy_file(o.in, out) != 0) {
        fprintf(stderr, "error: cannot write %s: %s\n", out, strerror(errno));
        return 1;
    }
    f = xfopen(out, "r+b");
    if (f == NULL) {
        return fail("cannot reopen the output");
    }
    {
        Iso outiso;
        Placement plan;
        if (iso_open(&outiso, f, err, sizeof(err)) != 0) {
            fclose(f);
            return fail(err);
        }
        if (iso_plan(&outiso, outiso.dol_off + dolsize, dol.size,
                     o.max_size, &plan) != 0)
        {
            fclose(f);
            xremove(out);
            return fail("no room for the patched executable in this image");
        }
        if (plan.fst != outiso.fst_off) {
            /* Strategy 2: the file table moves, its size stays. */
            uint8_t* fst = malloc(outiso.fst_size);
            uint8_t w[4];
            int bad = fst == NULL ||
                      iso_read_at(f, outiso.fst_off, fst, outiso.fst_size) != 0 ||
                      iso_write_at(f, plan.fst, fst, outiso.fst_size) != 0;
            put_be32(w, (uint32_t) plan.fst);
            bad = bad || iso_write_at(f, 0x424, w, 4) != 0;
            free(fst);
            if (bad) {
                fclose(f);
                xremove(out);
                return fail("write failed");
            }
        }
        where = plan.dol;
        if (iso_write_at(f, where, dol.data, dol.size) != 0) {
            fclose(f);
            xremove(out);
            return fail("write failed");
        }
        if (where != outiso.dol_off) {
            uint8_t w[4];
            put_be32(w, (uint32_t) where);
            if (iso_write_at(f, 0x420, w, 4) != 0) {
                fclose(f);
                xremove(out);
                return fail("write failed");
            }
        }
        iso_close(&outiso);
    }
    if (fclose(f) != 0) {
        xremove(out);
        return fail("write failed");
    }
    iso_close(&iso);
    printf("Done.  MeleeTag build %08X, cooldown %ld frames%s%s.\n"
           "Executable at 0x%llX (%lu bytes).\n",
           TAG_PAYLOAD_BUILD_ID, o.cooldown, o.hud ? "" : ", no HUD",
           o.netsync ? "" : ", netplay sync off", (unsigned long long) where,
           (unsigned long) dol.size);
    dol_free(&dol);
    free(img);
    return 0;
}
