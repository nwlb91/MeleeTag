#ifndef MELEETAG_ISO_H
#define MELEETAG_ISO_H

#define _FILE_OFFSET_BITS 64
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* A full GameCube disc; images may be trimmed but never longer. */
#define GCM_MAX_SIZE 1459978240ull
#define GCM_HEADER 0x440
#define GCM_APPLOADER 0x2440
#define GCM_ALIGN 0x8000u

typedef struct Range {
    uint64_t start, end;
} Range;

typedef struct Iso {
    FILE* f;
    uint64_t size;
    uint8_t header[GCM_HEADER];
    uint32_t dol_off;
    uint32_t fst_off, fst_size;
    Range* used;
    size_t nused;
} Iso;

int iso_seek(FILE* f, uint64_t off);
int iso_read_at(FILE* f, uint64_t off, void* buf, size_t n);
int iso_write_at(FILE* f, uint64_t off, const void* buf, size_t n);

/* Reads the disc header and the file table.  0 on success. */
int iso_open(Iso* iso, FILE* f, char* err, size_t errlen);
void iso_close(Iso* iso);

typedef struct Placement {
    uint64_t dol;      /* new DOL offset */
    uint64_t fst;      /* FST offset (moved only by strategy 2) */
    int strategy;      /* 1, 2 or 3 below */
} Placement;

/*
 * Where a DOL of `need` bytes goes, in order of preference:
 *   1. where the old one is, if the space after it is free;
 *   2. where the old one is, moving the file table (small) out of the way
 *      to the first free spot that fits it;
 *   3. at the first free spot after the old DOL, or after the last used
 *      byte, as long as the image stays within `max_size`.
 * The old DOL's bytes count as free (nothing references them afterwards).
 */
int iso_plan(const Iso* iso, uint64_t old_dol_end, uint64_t need,
             uint64_t max_size, Placement* out);

#endif
