/* GameCube DOL executable handling. */
#include "dol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t be32(const uint8_t* p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | p[3];
}

void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

size_t dol_extent(const uint8_t* hdr)
{
    size_t end = DOL_HEADER;
    int i;

    for (i = 0; i < DOL_SECTIONS; i++) {
        uint32_t off = be32(hdr + i * 4);
        uint32_t len = be32(hdr + 0x90 + i * 4);
        if (len != 0 && off + (size_t) len > end) {
            end = off + (size_t) len;
        }
    }
    return end;
}

int dol_parse(Dol* d, uint8_t* data, size_t size, char* err, size_t errlen)
{
    int i;

    memset(d, 0, sizeof(*d));
    d->data = data;
    d->size = size;
    if (size < DOL_HEADER) {
        snprintf(err, errlen, "DOL too small");
        return -1;
    }
    for (i = 0; i < DOL_SECTIONS; i++) {
        d->off[i] = be32(data + i * 4);
        d->addr[i] = be32(data + 0x48 + i * 4);
        d->len[i] = be32(data + 0x90 + i * 4);
        if (d->len[i] != 0 && (size_t) d->off[i] + d->len[i] > size) {
            snprintf(err, errlen, "DOL section %d runs past the file", i);
            return -1;
        }
    }
    d->bss_addr = be32(data + 0xD8);
    d->bss_len = be32(data + 0xDC);
    d->entry = be32(data + 0xE0);
    return 0;
}

void dol_free(Dol* d)
{
    free(d->data);
    d->data = NULL;
}

size_t dol_offset_of(const Dol* d, uint32_t addr, uint32_t span)
{
    int i;

    for (i = 0; i < DOL_SECTIONS; i++) {
        if (d->len[i] != 0 && addr >= d->addr[i] &&
            (uint64_t) addr + span <= (uint64_t) d->addr[i] + d->len[i])
        {
            return d->off[i] + (addr - d->addr[i]);
        }
    }
    return 0;
}

static int overlaps(uint32_t a, uint32_t alen, uint32_t b, uint32_t blen)
{
    return alen != 0 && blen != 0 && (uint64_t) a < (uint64_t) b + blen &&
           (uint64_t) b < (uint64_t) a + alen;
}

void dol_write_header(Dol* d)
{
    int i;

    for (i = 0; i < DOL_SECTIONS; i++) {
        put_be32(d->data + i * 4, d->off[i]);
        put_be32(d->data + 0x48 + i * 4, d->addr[i]);
        put_be32(d->data + 0x90 + i * 4, d->len[i]);
    }
    put_be32(d->data + 0xD8, d->bss_addr);
    put_be32(d->data + 0xDC, d->bss_len);
    put_be32(d->data + 0xE0, d->entry);
}

int dol_add_text(Dol* d, uint32_t addr, const uint8_t* bytes, uint32_t len,
                 char* err, size_t errlen)
{
    size_t off = (d->size + 31) & ~(size_t) 31;
    uint32_t padded = (len + 31) & ~31u;
    uint8_t* grown;
    int slot = -1;
    int i;

    for (i = 0; i < DOL_SECTIONS; i++) {
        if (overlaps(addr, padded, d->addr[i], d->len[i])) {
            snprintf(err, errlen,
                     "payload at %08X overlaps DOL section %d", addr, i);
            return -1;
        }
    }
    if (overlaps(addr, padded, d->bss_addr, d->bss_len)) {
        snprintf(err, errlen, "payload at %08X overlaps the bss", addr);
        return -1;
    }
    for (i = 0; i < DOL_TEXT; i++) {
        if (d->len[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        snprintf(err, errlen, "no free text section in the DOL");
        return -1;
    }
    grown = realloc(d->data, off + padded);
    if (grown == NULL) {
        snprintf(err, errlen, "out of memory");
        return -1;
    }
    memset(grown + d->size, 0, off + padded - d->size);
    memcpy(grown + off, bytes, len);
    d->data = grown;
    d->size = off + padded;
    d->off[slot] = (uint32_t) off;
    d->addr[slot] = addr;
    d->len[slot] = padded;
    dol_write_header(d);
    return 0;
}
