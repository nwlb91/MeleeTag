/*
 * Runs the real payload's boot relocator on PowerPC (qemu-ppc user mode).
 *
 * GameCube RAM is mapped at 0x80000000 and the few engine functions the
 * boot path calls are replaced by stubs at their retail addresses.  The
 * test loads build/payload.bin at TAG_LINK_BASE the way the DOL loader
 * would, fills the fields the patcher fills, enters through the boot entry,
 * and then checks the result: the copy at the top of the arena, applied
 * relocations, hook branches and trampolines, the restored boot site, and
 * that relocated code runs correctly (including jump tables).
 *
 *   boot_test <payload.bin> normal|small-arena
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "boot_syms.h" /* generated: addresses from the link and symbols */
#include "tag_rules.h"

#define RAM 0x80000000u
#define RAM_SIZE 0x01800000u
#define LINK 0x80A00000u
#define ARENA_HI_VAR 0x80001000u
#define ARENA_LO_VAR 0x80001004u
#define MFLR_R0 0x7C0802A6u
#define BLR 0x4E800020u

static int failures;

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static volatile uint32_t* word(uint32_t addr)
{
    return (volatile uint32_t*) (uintptr_t) addr;
}

static void put_code(uint32_t addr, const uint32_t* insns, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        *word(addr + 4 * i) = insns[i];
    }
    __builtin___clear_cache((char*) (uintptr_t) addr,
                            (char*) (uintptr_t) (addr + 4 * n));
}

static uint32_t branch_target(uint32_t at, uint32_t insn)
{
    int32_t disp = (int32_t) ((insn & 0x03FFFFFCu) << 6) >> 6;
    return at + (uint32_t) disp;
}

/*
 * The retail OSGetArenaHi/OSGetArenaLo/OSSetArenaHi are two instructions
 * each, eight bytes apart, so each entry is a single branch to a full stub
 * placed in otherwise unused low memory.
 */
static void stub_at(uint32_t entry, uint32_t body, const uint32_t* insns,
                    int n)
{
    uint32_t b = 0x48000000u | ((body - entry) & 0x03FFFFFCu);
    put_code(body, insns, n);
    put_code(entry, &b, 1);
}

static void stubs(void)
{
    /* OSGetArenaHi / OSGetArenaLo: lis r4,0x8000; lwz r3,off(r4); blr */
    static const uint32_t get_hi[] = { 0x3C808000, 0x80641000, BLR };
    static const uint32_t get_lo[] = { 0x3C808000, 0x80641004, BLR };
    /* OSSetArenaHi: lis r4,0x8000; stw r3,0x1000(r4); blr */
    static const uint32_t set_hi[] = { 0x3C808000, 0x90641000, BLR };
    static const uint32_t ret[] = { BLR };
    /* memcpy(r3 dst, r4 src, r5 n): byte loop, returns dst */
    static const uint32_t copy[] = {
        0x7C661B78, /* mr r6,r3 */
        0x2C050000, /* cmpwi r5,0 */
        0x4D820020, /* beqlr */
        0x7CA903A6, /* mtctr r5 */
        0x3884FFFF, /* addi r4,r4,-1 */
        0x38C6FFFF, /* addi r6,r6,-1 */
        0x8C040001, /* lbzu r0,1(r4) */
        0x9C060001, /* stbu r0,1(r6) */
        0x4200FFF8, /* bdnz -8 */
        BLR,
    };
    stub_at(SYM_OSGetArenaHi, 0x80001100u, get_hi, 3);
    stub_at(SYM_OSGetArenaLo, 0x80001110u, get_lo, 3);
    stub_at(SYM_OSSetArenaHi, 0x80001120u, set_hi, 3);
    stub_at(SYM_DCFlushRange, 0x80001130u, ret, 1);
    stub_at(SYM_ICInvalidateRange, 0x80001140u, ret, 1);
    stub_at(SYM_memcpy, 0x80001200u, copy, 10);
}

int main(int argc, char** argv)
{
    FILE* f;
    long size;
    uint8_t* blob;
    uint32_t image_size, reloc_off, reloc_count, hooks, nhooks, i;
    uint32_t dest, delta, hi0, lo0;
    int small;
    void* ram;

    if (argc != 3) {
        fprintf(stderr, "usage: boot_test payload.bin normal|small-arena\n");
        return 2;
    }
    small = strcmp(argv[2], "small-arena") == 0;
    ram = mmap((void*) (uintptr_t) RAM, RAM_SIZE,
               PROT_READ | PROT_WRITE | PROT_EXEC,
               MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ram == MAP_FAILED) {
        perror("mmap");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    blob = malloc((size_t) size);
    if (fread(blob, 1, (size_t) size, f) != (size_t) size) {
        return 2;
    }
    fclose(f);

    stubs();
    /* Engine words the boot path touches. */
    *word(SYM_BOOT_SITE) = 0xDEADBEEF; /* our branch would be here */
    for (i = 0; i < SYM_HOOK_COUNT; i++) {
        *word(sym_hook_sites[i]) = MFLR_R0;
    }

    /* The DOL loader puts the section at the link address... */
    memcpy((void*) (uintptr_t) LINK, blob, (size_t) size);
    /* ...and the patcher fills these. */
    *word(LINK + 0x28) = BLR; /* boot_orig: "HSD_OSInit" just returns */
    hooks = *word(LINK + 0x14) - LINK;
    nhooks = *word(LINK + 0x18);
    for (i = 0; i < nhooks; i++) {
        *word(LINK + hooks + i * 16 + 12) = MFLR_R0;
    }
    __builtin___clear_cache((char*) (uintptr_t) LINK,
                            (char*) (uintptr_t) (LINK + (uint32_t) size));

    hi0 = small ? 0x80B00000u : 0x81700000u;
    lo0 = 0x80500000u;
    *word(ARENA_HI_VAR) = hi0;
    *word(ARENA_LO_VAR) = lo0;
    image_size = *word(LINK + 8);
    reloc_off = *word(LINK + 12);
    reloc_count = *word(LINK + 16);
    CHECK(*word(LINK) == 0x4D544147u);
    CHECK(nhooks == SYM_HOOK_COUNT);

    /* Boot. */
    ((void (*)(void))(uintptr_t) * word(LINK + 0x20))();

    CHECK(*word(SYM_BOOT_SITE) == BLR); /* restored in every outcome */

    if (small) {
        /* Not enough arena: completely vanilla. */
        CHECK(*word(ARENA_HI_VAR) == hi0);
        for (i = 0; i < SYM_HOOK_COUNT; i++) {
            CHECK(*word(sym_hook_sites[i]) == MFLR_R0);
        }
        printf("boot_test small-arena: %s\n", failures ? "FAILED" : "ok");
        return failures != 0;
    }

    dest = ((hi0 & ~31u) - ((image_size + 31u) & ~31u)) & ~31u;
    delta = dest - LINK;
    CHECK(*word(ARENA_HI_VAR) == dest);
    CHECK(*word(dest) == 0x4D544147u);

    /* Hook table moved with the image; sites branch into the copy. */
    for (i = 0; i < nhooks; i++) {
        uint32_t h = dest + hooks + i * 16;
        uint32_t site = *word(h), handler = *word(h + 4), tramp = *word(h + 8);
        CHECK(site == sym_hook_sites[i]);
        CHECK(handler >= dest && handler < dest + image_size);
        CHECK(handler == *word(LINK + hooks + i * 16 + 4) + delta);
        CHECK((*word(site) >> 26) == 18);
        CHECK(branch_target(site, *word(site)) == handler);
        CHECK(tramp == *word(LINK + hooks + i * 16 + 8) + delta);
        CHECK(*word(tramp) == MFLR_R0);
        CHECK(branch_target(tramp + 4, *word(tramp + 4)) == site + 4);
    }

    /* Every branch into engine code was re-aimed from the new place. */
    for (i = 0; i < reloc_count; i++) {
        uint32_t kind = blob[reloc_off + i * 8];
        uint32_t off = *(uint32_t*) (blob + reloc_off + i * 8) & 0x00FFFFFFu;
        uint32_t value = *(uint32_t*) (blob + reloc_off + i * 8 + 4);
        uint32_t at = dest + off;
        if (kind == 5) {
            CHECK(branch_target(at, *word(at)) == value);
        } else if (kind == 1) {
            CHECK(*word(at) == value + delta);
        }
    }

    /* tag_init ran in the copy: tag_g.cfg points at the moved config. */
    CHECK(*word(SYM_tag_g + delta) == SYM_tag_config + delta);
    CHECK(*word(SYM_tag_config + delta + 4) == 30); /* default cooldown */

    /* Relocated code works, including switch jump tables. */
    {
        typedef unsigned (*MapJumps)(int, unsigned, unsigned, unsigned);
        typedef int (*StateClass)(int);
        typedef int (*Entry)(const TagFighterView*);
        MapJumps mj = (MapJumps) (uintptr_t) (SYM_tag_rules_map_jumps + delta);
        StateClass sc = (StateClass) (uintptr_t) (SYM_tag_rules_state_class + delta);
        Entry en = (Entry) (uintptr_t) (SYM_tag_rules_entry + delta);
        TagFighterView v;

        CHECK(mj(1, 1, 2, 6) == 1);
        CHECK(mj(1, 2, 2, 6) == 6);
        CHECK(mj(0, 2, 2, 6) == 0);
        CHECK(sc(TAG_MS_WAIT) == TAG_CLASS_GROUND);
        CHECK(sc(TAG_MS_SQUATWAIT) == TAG_CLASS_CROUCH);
        CHECK(sc(TAG_MS_DAMAGEFALL) == TAG_CLASS_TUMBLE);
        CHECK(sc(TAG_MS_ATTACKAIRN) == TAG_CLASS_AIR_IASA);
        CHECK(sc(500) == TAG_CLASS_NEVER);
        memset(&v, 0, sizeof(v));
        v.motion_id = TAG_MS_LANDING;
        v.landing_allowed = 1;
        v.anim_frame = 5.0F;
        v.landing_lag = 4.0F;
        CHECK(en(&v) == TAG_ENTRY_WAIT);
        v.motion_id = TAG_MS_DAMAGEFLYHI;
        v.airborne = 1;
        CHECK(en(&v) == TAG_ENTRY_TUMBLE);
    }

    printf("boot_test normal: payload %u bytes at %08X, %u relocations, "
           "%u hooks: %s\n",
           image_size, dest, reloc_count, nhooks, failures ? "FAILED" : "ok");
    return failures != 0;
}
