#ifndef MELEETAG_PAYLOAD_H
#define MELEETAG_PAYLOAD_H

/*
 * Layout of the MeleeTag payload image.
 *
 * The patcher stores the image in a new DOL section that the apploader loads
 * at TAG_LINK_BASE.  That address is only scratch space: it lies in the
 * future heap area, which nothing touches until HSD_OSInit builds the heaps.
 * The patcher redirects HSD_OSInit's first instruction to the boot stub,
 * which copies the image to the top of the OS arena, applies the relocation
 * table, lowers the arena's high bound below the copy, installs the runtime
 * hooks and then re-enters HSD_OSInit with its original instruction restored.
 *
 * Reserving memory from the top of the arena (instead of the bottom, where
 * Slippi places its own codeset) keeps every low-arena heap at the address
 * Slippi's rollback savestates expect.  See docs/DESIGN.md, "Code placement".
 *
 * The section on disc is [image][relocation table]; only the image is copied
 * to the arena.  Addresses stored in the header and hook table are link-time
 * (TAG_LINK_BASE-relative) addresses, relocated like any other pointer.
 *
 * This header is shared by the PowerPC payload and the host-side patcher, so
 * it only uses 32-bit fields; on disc every field is big-endian.
 */

#define TAG_PAYLOAD_MAGIC 0x4D544147u /* 'MTAG' */
#define TAG_PAYLOAD_VERSION 1u

/* Scratch load address of the payload section (see above). */
#define TAG_LINK_BASE 0x80A00000u

/* First instruction of HSD_OSInit; the only branch the patcher writes. */
#define TAG_BOOT_SITE 0x80375304u

/* Relocation kinds; each entry is two words: (kind << 24 | offset), value. */
enum {
    TAG_REL_ADDR32 = 1, /* word holds an address inside the image */
    TAG_REL_LO = 2,     /* halfword: low 16 bits of an image address */
    TAG_REL_HA = 3,     /* halfword: high-adjusted 16 bits of an image address */
    TAG_REL_HI = 4,     /* halfword: high 16 bits of an image address */
    TAG_REL_REL24 = 5,  /* b/bl from the image to a fixed engine address */
};

typedef struct TagPayloadHeader {
    /* 0x00 */ unsigned int magic;
    /* 0x04 */ unsigned int version;
    /* 0x08 */ unsigned int image_size; /* copied bytes; build tool fills */
    /* 0x0C */ unsigned int reloc_off;  /* from header; build tool fills */
    /* 0x10 */ unsigned int reloc_count;
    /* 0x14 */ unsigned int hooks; /* TagHook[] (link address) */
    /* 0x18 */ unsigned int hook_count;
    /* 0x1C */ unsigned int init;       /* tag_init (link address) */
    /* 0x20 */ unsigned int boot_entry; /* boot stub (link address) */
    /* 0x24 */ unsigned int boot_site;  /* TAG_BOOT_SITE */
    /* 0x28 */ unsigned int boot_orig;  /* patcher: original insn there */
    /* 0x2C */ unsigned int config;     /* TagConfig (link address) */
    /* 0x30 */ unsigned int build_id;   /* identifies the payload build */
    /* 0x34 */ unsigned int reserved[3];
} TagPayloadHeader;

/* One function-entry detour.  The patcher fills orig_insn from the DOL. */
typedef struct TagHook {
    unsigned int site;      /* engine address of the patched instruction */
    unsigned int handler;   /* branch target (link address) */
    unsigned int tramp;     /* 2-word trampoline or 0 (link address) */
    unsigned int orig_insn; /* original instruction at site */
} TagHook;

/* User-tunable settings, stored in the image so the patcher can edit them. */
typedef struct TagConfig {
    unsigned int magic;           /* 'MCFG' */
    unsigned int cooldown_frames; /* shared switch cooldown, default 30 */
    unsigned int flags;           /* TAG_CFG_* */
    unsigned int max_fighters;    /* fighter objects alive at once, all players */
    unsigned int reserved[4];
} TagConfig;

#define TAG_CONFIG_MAGIC 0x4D434647u /* 'MCFG' */
#define TAG_CFG_HUD (1u << 0)        /* draw the in-match slot HUD */
#define TAG_CFG_NETSYNC (1u << 1)    /* Slippi online roster handshake */

#define TAG_DEFAULT_COOLDOWN 30u
#define TAG_MAX_COOLDOWN 600u

/*
 * Vanilla already runs eight fighter objects in a four-player match of
 * Zeldas or Ice Climbers, so eight is a load the heap is known to carry.
 */
#define TAG_DEFAULT_MAX_FIGHTERS 8u
#define TAG_MIN_FIGHTERS 4u
#define TAG_MAX_FIGHTERS 12u

#endif
