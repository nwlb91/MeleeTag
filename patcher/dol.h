#ifndef MELEETAG_DOL_H
#define MELEETAG_DOL_H

#include <stddef.h>
#include <stdint.h>

#define DOL_TEXT 7
#define DOL_DATA 11
#define DOL_SECTIONS (DOL_TEXT + DOL_DATA)
#define DOL_HEADER 0x100

/* A DOL held in memory; sections 0..6 are text, 7..17 data. */
typedef struct Dol {
    uint8_t* data;
    size_t size;
    uint32_t off[DOL_SECTIONS];
    uint32_t addr[DOL_SECTIONS];
    uint32_t len[DOL_SECTIONS];
    uint32_t bss_addr, bss_len, entry;
} Dol;

uint32_t be32(const uint8_t* p);
void put_be32(uint8_t* p, uint32_t v);

/* Size of the DOL whose header is at `hdr` (end of its last section). */
size_t dol_extent(const uint8_t* hdr);

/* Parse; takes ownership of `data` (malloc'd).  0 on success. */
int dol_parse(Dol* d, uint8_t* data, size_t size, char* err, size_t errlen);
void dol_free(Dol* d);

/* File offset of a loaded address, or 0 if no section covers it. */
size_t dol_offset_of(const Dol* d, uint32_t addr, uint32_t span);

/*
 * Append `len` bytes as a new text section loaded at `addr`.  Fails if no
 * text slot is free or the range overlaps an existing section or the bss.
 */
int dol_add_text(Dol* d, uint32_t addr, const uint8_t* bytes, uint32_t len,
                 char* err, size_t errlen);

void dol_write_header(Dol* d);

#endif
