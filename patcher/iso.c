/* GameCube disc image (GCM/ISO) layout. */
#include "iso.h"

#include <stdlib.h>
#include <string.h>

#include "dol.h"

#ifdef _WIN32
#define tag_fseek _fseeki64
#else
#define tag_fseek fseeko
#endif

int iso_seek(FILE* f, uint64_t off)
{
    return tag_fseek(f, (long long) off, SEEK_SET);
}

int iso_read_at(FILE* f, uint64_t off, void* buf, size_t n)
{
    if (iso_seek(f, off) != 0) {
        return -1;
    }
    return fread(buf, 1, n, f) == n ? 0 : -1;
}

int iso_write_at(FILE* f, uint64_t off, const void* buf, size_t n)
{
    if (iso_seek(f, off) != 0) {
        return -1;
    }
    return fwrite(buf, 1, n, f) == n ? 0 : -1;
}

static int add_range(Iso* iso, uint64_t start, uint64_t len)
{
    Range* grown;

    if (len == 0) {
        return 0;
    }
    grown = realloc(iso->used, (iso->nused + 1) * sizeof(Range));
    if (grown == NULL) {
        return -1;
    }
    iso->used = grown;
    iso->used[iso->nused].start = start;
    iso->used[iso->nused].end = start + len;
    iso->nused++;
    return 0;
}

static int cmp_range(const void* a, const void* b)
{
    const Range* x = a;
    const Range* y = b;
    if (x->start != y->start) {
        return x->start < y->start ? -1 : 1;
    }
    return 0;
}

int iso_open(Iso* iso, FILE* f, char* err, size_t errlen)
{
    uint8_t app[0x20];
    uint8_t dolhdr[DOL_HEADER];
    uint8_t* fst;
    uint32_t count, i;

    memset(iso, 0, sizeof(*iso));
    iso->f = f;
    if (tag_fseek(f, 0, SEEK_END) != 0) {
        snprintf(err, errlen, "cannot seek the image");
        return -1;
    }
#ifdef _WIN32
    iso->size = (uint64_t) _ftelli64(f);
#else
    iso->size = (uint64_t) ftello(f);
#endif
    if (iso->size < GCM_APPLOADER + 0x20 ||
        iso_read_at(f, 0, iso->header, GCM_HEADER) != 0 ||
        iso_read_at(f, GCM_APPLOADER, app, sizeof(app)) != 0)
    {
        snprintf(err, errlen, "not a GameCube disc image (too small)");
        return -1;
    }
    iso->dol_off = be32(iso->header + 0x420);
    iso->fst_off = be32(iso->header + 0x424);
    iso->fst_size = be32(iso->header + 0x428);
    if ((uint64_t) iso->dol_off + DOL_HEADER > iso->size ||
        (uint64_t) iso->fst_off + iso->fst_size > iso->size || iso->fst_size < 12)
    {
        snprintf(err, errlen, "disc header points outside the image");
        return -1;
    }

    /* Boot block, bi2 and apploader. */
    add_range(iso, 0, GCM_APPLOADER + 0x20 + (uint64_t) be32(app + 0x14) +
                          be32(app + 0x18));
    if (iso_read_at(f, iso->dol_off, dolhdr, sizeof(dolhdr)) != 0) {
        snprintf(err, errlen, "cannot read the DOL header");
        return -1;
    }
    add_range(iso, iso->dol_off, dol_extent(dolhdr));
    add_range(iso, iso->fst_off, iso->fst_size);

    fst = malloc(iso->fst_size);
    if (fst == NULL || iso_read_at(f, iso->fst_off, fst, iso->fst_size) != 0) {
        free(fst);
        snprintf(err, errlen, "cannot read the file table");
        return -1;
    }
    count = be32(fst + 8);
    if ((uint64_t) count * 12 > iso->fst_size) {
        free(fst);
        snprintf(err, errlen, "corrupt file table");
        return -1;
    }
    for (i = 1; i < count; i++) {
        const uint8_t* e = fst + i * 12;
        if (e[0] == 0) { /* file */
            add_range(iso, be32(e + 4), be32(e + 8));
        }
    }
    free(fst);
    qsort(iso->used, iso->nused, sizeof(Range), cmp_range);
    return 0;
}

void iso_close(Iso* iso)
{
    free(iso->used);
    iso->used = NULL;
    iso->nused = 0;
}

static uint64_t align_to(uint64_t v, uint64_t a)
{
    return (v + a - 1) & ~(a - 1);
}

static int overlap(uint64_t a, uint64_t alen, uint64_t b, uint64_t bend)
{
    return a < bend && b < a + alen;
}

/* Is [pos, pos+need) clear of every used range not in `ignore`? */
static int is_free(const Iso* iso, const Range* ignore, int nignore,
                   const Range* reserve, uint64_t pos, uint64_t need,
                   uint64_t max_size)
{
    size_t i;
    int j;

    if (pos + need > max_size) {
        return 0;
    }
    if (reserve != NULL && overlap(pos, need, reserve->start, reserve->end)) {
        return 0;
    }
    for (i = 0; i < iso->nused; i++) {
        const Range* r = &iso->used[i];
        int skip = 0;
        for (j = 0; j < nignore; j++) {
            if (r->start == ignore[j].start && r->end == ignore[j].end) {
                skip = 1;
            }
        }
        if (!skip && overlap(pos, need, r->start, r->end)) {
            return 0;
        }
    }
    return 1;
}

/* Lowest free position >= from, trying `from` and every range end. */
static int find_free(const Iso* iso, const Range* ignore, int nignore,
                     const Range* reserve, uint64_t from, uint64_t need,
                     uint64_t align, uint64_t max_size, uint64_t* where)
{
    uint64_t best = UINT64_MAX;
    uint64_t cand;
    size_t i;

    cand = align_to(from, align);
    if (is_free(iso, ignore, nignore, reserve, cand, need, max_size)) {
        best = cand;
    }
    for (i = 0; i <= iso->nused; i++) {
        uint64_t end = i < iso->nused ? iso->used[i].end
                                      : (reserve ? reserve->end : 0);
        cand = align_to(end, align);
        if (cand < from || cand >= best) {
            continue;
        }
        if (is_free(iso, ignore, nignore, reserve, cand, need, max_size)) {
            best = cand;
        }
    }
    if (best == UINT64_MAX) {
        return -1;
    }
    *where = best;
    return 0;
}

int iso_plan(const Iso* iso, uint64_t old_dol_end, uint64_t need,
             uint64_t max_size, Placement* out)
{
    Range ignore[2];
    Range dol;

    ignore[0].start = iso->dol_off;
    ignore[0].end = old_dol_end;
    ignore[1].start = iso->fst_off;
    ignore[1].end = (uint64_t) iso->fst_off + iso->fst_size;
    out->fst = iso->fst_off;

    /* 1 */
    if (is_free(iso, ignore, 1, NULL, iso->dol_off, need, max_size)) {
        out->dol = iso->dol_off;
        out->strategy = 1;
        return 0;
    }
    /* 2 */
    dol.start = iso->dol_off;
    dol.end = iso->dol_off + need;
    if (is_free(iso, ignore, 2, NULL, iso->dol_off, need, max_size) &&
        find_free(iso, ignore, 2, &dol, GCM_APPLOADER, iso->fst_size, 0x20,
                  max_size, &out->fst) == 0)
    {
        out->dol = iso->dol_off;
        out->strategy = 2;
        return 0;
    }
    /* 3 */
    out->fst = iso->fst_off;
    if (find_free(iso, ignore, 1, NULL, iso->dol_off, need, GCM_ALIGN,
                  max_size, &out->dol) == 0)
    {
        out->strategy = 3;
        return 0;
    }
    return -1;
}
