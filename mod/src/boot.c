/*
 * Boot relocator.  Runs once, at TAG_LINK_BASE, from the HSD_OSInit entry
 * branch (see tag_payload.h for why the image moves).  Nothing here may be
 * called after tag_boot() returns: the scratch copy is reused by the heaps.
 */
#include <dolphin/os.h>
#include <dolphin/os/OSCache.h>
#include <string.h>

#include "tag_payload.h"

extern TagPayloadHeader tag_payload_header;

/* Never take memory if less than this would remain for the HSD heaps. */
#define TAG_MIN_FREE_ARENA 0x00800000u

static unsigned int branch_insn(unsigned int from, unsigned int to)
{
    return 0x48000000u | ((to - from) & 0x03FFFFFCu);
}

static void write_insn(unsigned int addr, unsigned int insn)
{
    *(volatile unsigned int*) addr = insn;
    DCFlushRange((void*) addr, 4);
    ICInvalidateRange((void*) addr, 4);
}

static void apply_relocs(unsigned int dest, const unsigned int* rel,
                         unsigned int count)
{
    unsigned int delta = dest - TAG_LINK_BASE;
    unsigned int i;

    for (i = 0; i < count; i++, rel += 2) {
        unsigned int kind = rel[0] >> 24;
        unsigned int at = dest + (rel[0] & 0x00FFFFFFu);
        unsigned int value = rel[1] + delta;

        switch (kind) {
        case TAG_REL_ADDR32:
            *(unsigned int*) at = value;
            break;
        case TAG_REL_LO:
            *(unsigned short*) at = (unsigned short) value;
            break;
        case TAG_REL_HA:
            *(unsigned short*) at =
                (unsigned short) ((value >> 16) + ((value & 0x8000u) ? 1 : 0));
            break;
        case TAG_REL_HI:
            *(unsigned short*) at = (unsigned short) (value >> 16);
            break;
        case TAG_REL_REL24: {
            /* rel[1] is a fixed engine address; only the branch moved. */
            unsigned int insn = *(unsigned int*) at;
            *(unsigned int*) at =
                (insn & 0xFC000003u) | ((rel[1] - at) & 0x03FFFFFCu);
            break;
        }
        default:
            break;
        }
    }
}

void tag_boot(void)
{
    const TagPayloadHeader* hdr = &tag_payload_header;
    unsigned int size = (hdr->image_size + 31u) & ~31u;
    unsigned int hi = (unsigned int) OSGetArenaHi() & ~31u;
    unsigned int lo = (unsigned int) OSGetArenaLo();
    unsigned int dest = (hi - size) & ~31u;
    unsigned int delta = dest - TAG_LINK_BASE;
    const TagHook* hooks;
    unsigned int i;

    /* HSD_OSInit runs unmodified from here on in every outcome. */
    write_insn(hdr->boot_site, hdr->boot_orig);

    if (hdr->magic != TAG_PAYLOAD_MAGIC || hi <= lo || dest <= lo ||
        dest - lo < TAG_MIN_FREE_ARENA)
    {
        /* Not enough room: leave the game completely vanilla. */
        return;
    }

    memcpy((void*) dest, hdr, size);
    apply_relocs(dest,
                 (const unsigned int*) ((const char*) hdr + hdr->reloc_off),
                 hdr->reloc_count);
    DCFlushRange((void*) dest, size);
    ICInvalidateRange((void*) dest, size);

    OSSetArenaHi((void*) dest);

    /* The relocated copy's hook table already holds final addresses. */
    hooks = (const TagHook*) (hdr->hooks + delta);
    for (i = 0; i < hdr->hook_count; i++) {
        const TagHook* h = &hooks[i];
        if (h->tramp != 0) {
            write_insn(h->tramp, h->orig_insn);
            write_insn(h->tramp + 4, branch_insn(h->tramp + 4, h->site + 4));
        }
        write_insn(h->site, branch_insn(h->site, h->handler));
    }

    ((void (*)(void))(hdr->init + delta))();
}
