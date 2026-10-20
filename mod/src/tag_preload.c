/*
 * Getting extra roster characters into memory before the match starts.
 *
 * Melee loads fighter files through its preload cache: each scene declares
 * the files it wants (lbDvd_80017960 registers the "game cache": stage and
 * four characters) and the cache loads them in the background into two
 * heaps, AllM (main RAM: model, costume, effects, Kirby hats) and AllA
 * (ARAM: animations).  The cache compacts a heap before every load, so the
 * heap is full exactly when the sizes of the registered files add up to its
 * size, and the allocator asserts (crash) if a file does not fit.  Nothing
 * may be loaded once a match runs: compaction would move archives in use,
 * and Slippi's rollback does not cover these heaps.
 *
 * So the extra characters join the scene's own registration, one at a time
 * in a fixed order, each kept only if the registered total still fits both
 * heaps (exact sizes from the disc's file table, not estimates).  Sound
 * banks are admitted the same way when the match loads its banks.  A
 * character that does not fit is shown as unavailable for that match; the
 * first slot of every roster is offered memory before any second slot.
 */
#include <string.h>

#include "tag.h"
#include "tag_hook.h"

#define TAG_HEAP_MARGIN 0x10000u /* never fill a preload heap completely */
#define TAG_ENTRIES_SPARE 4

static PreloadEntry tag_entries_snapshot[80];

/* ---- File lookup -------------------------------------------------------- */

static int tag_entry_num(const char* name)
{
    return DVDConvertPathToEntrynum(lbFileGetFullName(name));
}

/* Registered for the current scene (positive score) in `heap`. */
static int tag_file_registered(const char* name)
{
    int num;
    int i;

    if (name == NULL) {
        return 1;
    }
    num = tag_entry_num(name);
    if (num < 0) {
        return 0;
    }
    for (i = 0; i < 80; i++) {
        PreloadEntry* e = &preloadCache.entries[i];
        if (e->state != 0 && e->load_score > 0 && e->entry_num == num) {
            return 1;
        }
    }
    return 0;
}

static int tag_clamp_costume(int ftkind, int costume)
{
    if (costume >= CostumeListsForeachCharacter[ftkind].numCostumes) {
        return 0;
    }
    return costume;
}

/* The files ftData_800855C8 registers for one fighter kind. */
static int tag_ftkind_ready(int ftkind, int costume)
{
    u8 ef;

    costume = tag_clamp_costume(ftkind, costume);
    if (!tag_file_registered(ftData_803C1F40[ftkind].a) ||
        !tag_file_registered(ftData_803C2360[ftkind][costume].dat_filename) ||
        !tag_file_registered(ftData_803C23E4[ftkind]))
    {
        return 0;
    }
    ef = ftData_UnkBytePerCharacter[ftkind];
    if (ef != 0xFF && !tag_file_registered(efAsync_DatEntries[ef].ef_DAT_file))
    {
        return 0;
    }
    return 1;
}

int tag_preload_char_ready(int ckind, int costume)
{
    TagFtMapping* map;

    if (!tag_ckind_valid(ckind)) {
        return 0;
    }
    map = &tag_ft_mapping[ckind];
    if (!tag_ftkind_ready(map->internal_id, costume)) {
        return 0;
    }
    if (map->extra_internal_id != -1 &&
        !tag_ftkind_ready(map->extra_internal_id, costume))
    {
        return 0;
    }
    return 1;
}

/* The files ftKb_SpecialN_800EEC34 registers for Kirby copying `ftkind`. */
static int tag_hat_ready(int ftkind, int kirby_costume)
{
    u8 ef;

    if (ftkind < 0 || ftkind == Ft_Kind_Kirby) {
        return 1;
    }
    if (!tag_file_registered(ftKb_Init_803CA9D0[ftkind].filename)) {
        return 0;
    }
    if (ftKb_Init_803CB3E8[ftkind] != NULL &&
        !tag_file_registered(
            ftKb_Init_803CB3E8[ftkind][tag_clamp_costume(Ft_Kind_Kirby,
                                                          kirby_costume)]
                .dat_filename))
    {
        return 0;
    }
    ef = ftKb_Init_803CB46C[ftkind];
    if (ef != 0xFF && !tag_file_registered(efAsync_DatEntries[ef].ef_DAT_file))
    {
        return 0;
    }
    return 1;
}

int tag_preload_kirby_ready(int ckind, int kirby_costume)
{
    TagFtMapping* map = &tag_ft_mapping[ckind];

    return tag_hat_ready(map->internal_id, kirby_costume) &&
           (map->extra_internal_id == -1 ||
            tag_hat_ready(map->extra_internal_id, kirby_costume));
}

/* ---- Heap accounting ---------------------------------------------------- */

static u32 tag_round32(u32 n)
{
    return (n + 31u) & ~31u;
}

/* Bytes the registered set occupies in `heap` once loaded and compacted. */
static u32 tag_heap_need(int heap)
{
    u32 total = 0;
    int i;

    for (i = 0; i < 80; i++) {
        PreloadEntry* e = &preloadCache.entries[i];
        u32 size;
        if (e->state == 0 || e->load_score <= 0 || e->heap != heap) {
            continue;
        }
        size = e->size != 0 ? e->size : lbFile_8001634C(e->entry_num);
        total += tag_round32(size);
        if (e->type == 2 || e->type == 3 || e->type == 4) {
            total += tag_round32(sizeof(HSD_Archive));
        }
    }
    return total;
}

static int tag_heaps_fit(void)
{
    static const int heaps[2] = { LbHeapKind_AllM, LbHeapKind_AllA };
    int i;

    for (i = 0; i < 2; i++) {
        TagLbHeap* h = &tag_lbheap.heap[heaps[i]];
        if (h->status != LbHeapStatus_Create || h->size <= TAG_HEAP_MARGIN) {
            return 0;
        }
        if (tag_heap_need(heaps[i]) > h->size - TAG_HEAP_MARGIN) {
            return 0;
        }
    }
    return 1;
}

static int tag_free_entries(void)
{
    int i, n = 0;
    for (i = 0; i < 80; i++) {
        if (preloadCache.entries[i].state == 0) {
            n++;
        }
    }
    return n;
}

/* ---- The plan ----------------------------------------------------------- */

typedef struct TagPlanCtx {
    u8 chars[8 + TAG_PORTS * 3]; /* CharacterKinds that will be loaded */
    u8 nchars;
    u8 kirby[8 + TAG_PORTS * 3]; /* Kirby costumes in play */
    u8 nkirby;
} TagPlanCtx;

static int tag_has(const u8* list, int n, int v)
{
    int i;
    for (i = 0; i < n; i++) {
        if (list[i] == v) {
            return 1;
        }
    }
    return 0;
}

/*
 * Register one extra character and every Kirby hat it brings along; keep the
 * registration only if both heaps still fit, otherwise roll the cache back.
 */
static int tag_try_admit(TagPlanCtx* c, int ck, int costume)
{
    int new_kirby = ck == TAG_CKIND_KIRBY && !tag_has(c->kirby, c->nkirby, costume);
    int hats = c->nkirby + (new_kirby ? c->nchars + 1 : 0);
    int i;

    /*
     * Upper bound on new cache entries: four files per fighter kind and
     * three per Kirby hat, doubled for two-fighter characters.  The cache
     * asserts when it runs out of its 80 entries.
     */
    if (tag_free_entries() < 8 + 6 * hats + TAG_ENTRIES_SPARE) {
        return 0;
    }
    memcpy(tag_entries_snapshot, preloadCache.entries,
           sizeof(tag_entries_snapshot));

    Player_80031CB0(ck, (u8) costume);
    for (i = 0; i < c->nkirby; i++) {
        Player_80031D2C(ck, c->kirby[i]);
    }
    if (new_kirby) {
        for (i = 0; i < c->nchars; i++) {
            Player_80031D2C(c->chars[i], (u8) costume);
        }
        Player_80031D2C(ck, (u8) costume);
    }
    if (!tag_heaps_fit()) {
        memcpy(preloadCache.entries, tag_entries_snapshot,
               sizeof(tag_entries_snapshot));
        return 0;
    }
    if (!tag_has(c->chars, c->nchars, ck)) {
        c->chars[c->nchars++] = (u8) ck;
    }
    if (new_kirby) {
        c->kirby[c->nkirby++] = (u8) costume;
    }
    return 1;
}

/*
 * Which rosters this scene preloads.  Local versus: the roster of every
 * human door that picked a character.  Online: ours and the ones learned
 * from the opponents last match, sorted so every client derives the same
 * plan from the same rosters regardless of port order.
 */
static void tag_plan_rosters(struct GameCache* gc)
{
    int i;

    tag_g.plan_rosters = 0;
    if (tag_scene_major() == TAG_MAJOR_ONLINE) {
        TagRosterExtras* r = tag_g.plan_roster;
        int n = 0;
        r[n++] = tag_g.net_local;
        for (i = 0; i < tag_g.learned_count && n < TAG_PORTS; i++) {
            r[n++] = tag_g.learned[i];
        }
        tag_rules_sort_rosters(r, NULL, n);
        for (i = 0; i < n; i++) {
            tag_g.plan_owner[i] = 0xFF;
        }
        tag_g.plan_rosters = (u8) n;
        tag_g.plan_netplay = 1;
        return;
    }
    tag_g.plan_netplay = 0;
    for (i = 0; i < TAG_PORTS; i++) {
        if (!tag_g.css_human[i] || gc->entries[i].char_id == ChKind_None ||
            tag_roster_empty(&tag_g.css[i]))
        {
            continue;
        }
        tag_g.plan_roster[tag_g.plan_rosters] = tag_g.css[i];
        tag_g.plan_owner[tag_g.plan_rosters] = (u8) i;
        tag_g.plan_rosters++;
    }
}

u16 tag_preload_plan_hash(void)
{
    return tag_rules_rosters_hash(tag_g.plan_roster, tag_g.plan_rosters);
}

static void tag_preload_register(void)
{
    struct GameCache* gc = &preloadCache.new_scene.game_cache;
    TagPlanCtx c;
    u8 order[TAG_PORTS * 3];
    int n, i, j;

    tag_g.plan_valid = 0;
    if (tag_scene_major() != TAG_MAJOR_VS &&
        tag_scene_major() != TAG_MAJOR_ONLINE)
    {
        return;
    }
    tag_plan_rosters(gc);
    for (i = 0; i < TAG_PORTS; i++) {
        for (j = 0; j < 3; j++) {
            tag_g.plan_admit[i][j] = 0;
            tag_g.sound_admit[i][j] = 0;
        }
    }

    /* What vanilla already registered: the players' own characters. */
    c.nchars = 0;
    c.nkirby = 0;
    for (i = 0; i < 8; i++) {
        int ck = gc->entries[i].char_id;
        if (!tag_ckind_valid(ck)) {
            continue;
        }
        if (!tag_has(c.chars, c.nchars, ck)) {
            c.chars[c.nchars++] = (u8) ck;
        }
        if (ck == TAG_CKIND_KIRBY &&
            !tag_has(c.kirby, c.nkirby, gc->entries[i].color))
        {
            c.kirby[c.nkirby++] = gc->entries[i].color;
        }
    }

    n = tag_rules_admission_order(tag_g.plan_rosters, order);
    for (i = 0; i < n; i++) {
        int r = order[i] / 3, k = order[i] % 3;
        int ck = tag_g.plan_roster[r].ckind[k];
        int co = tag_g.plan_roster[r].costume[k];
        if (!tag_ckind_valid(ck)) {
            continue;
        }
        tag_g.plan_admit[r][k] = (u8) tag_try_admit(&c, ck, co);
    }
    tag_g.plan_hash = tag_preload_plan_hash();
    tag_g.plan_valid = 1;
}

TAG_TRAMPOLINE(tag_tramp_lbDvd_80017960);

void tag_hook_lbDvd_80017960(void)
{
    TAG_CALL_ORIG(tag_tramp_lbDvd_80017960, void (*)(void))();
    tag_preload_register();
}

/*
 * Rosters changed on the CSS: make the next lbDvd_80018254 see a changed
 * scene so it re-registers (it compares new_scene with scene and then
 * overwrites new_scene, so this field never sticks).
 */
void tag_preload_request_refresh(void)
{
    preloadCache.new_scene.mode_scene_changes ^= 0x40000000;
    lbDvd_80018254();
}

/* ---- Sound banks -------------------------------------------------------- */

static int tag_bank_bit(u64 mask, int i)
{
    u32 half = i < 32 ? (u32) mask : (u32) (mask >> 32);
    return (half >> (i & 31)) & 1;
}

/*
 * Bytes the sound heap holds after lbAudioAx_80027168 with `extra` added to
 * the wanted banks: everything wanted, plus loaded banks it cannot evict
 * (fn_800267B0 only evicts groups 0-4).  It asserts when this exceeds the
 * capacity, so the same sum decides admission.
 */
static u32 tag_sound_need(u64 extra)
{
    u32 total = 0;
    int i;

    for (i = 0; i < TAG_SSM_BANKS - 1; i++) {
        int wanted = tag_ssm_want[i] != -1 || tag_bank_bit(extra, i);
        int loaded = tag_ssm_loaded[i] != -1;
        if (wanted ||
            (loaded && (tag_ssm_info[i][2] < 0 || tag_ssm_info[i][2] > 4)))
        {
            total += tag_ssm_size[i][0];
        }
    }
    return total;
}

static void tag_preload_sound(void)
{
    u8 order[TAG_PORTS * 3];
    u64 extra = 0;
    int n, i;

    if (!tag_g.plan_valid) {
        return;
    }
    n = tag_rules_admission_order(tag_g.plan_rosters, order);
    for (i = 0; i < n; i++) {
        int r = order[i] / 3, k = order[i] % 3;
        int ck = tag_g.plan_roster[r].ckind[k];
        u64 mask;
        if (!tag_g.plan_admit[r][k]) {
            continue;
        }
        mask = lbAudioAx_80026E84(ck);
        if (tag_sound_need(extra | mask) <= (u32) tag_ssm_capacity) {
            extra |= mask;
            tag_g.sound_admit[r][k] = 1;
        }
    }
    if (extra != 0) {
        lbAudioAx_8002702C(0x1F, extra);
        lbAudioAx_80027168();
        lbAudioAx_80027648();
    }
}

TAG_TRAMPOLINE(tag_tramp_lbAudioAx_8002785C);

void tag_hook_lbAudioAx_8002785C(void)
{
    TAG_CALL_ORIG(tag_tramp_lbAudioAx_8002785C, void (*)(void))();
    if (tag_g.loading_match) {
        tag_preload_sound();
    }
}

/* ---- Match load --------------------------------------------------------- */

/*
 * What Fighter_Create would initialise on first use: the fighter's data
 * archive, effect bank, costume model and animation table.  This is
 * ftLib_LoadFighterCostume without its last step (ftData_800857E0), which
 * for Kirby loads costume-0 copy hats for every player's character; versus
 * mode never runs it and those hats are not necessarily preloaded.  Kirby's
 * hats are initialised per costume below, as fn_8016E124 does.
 */
static void tag_load_kind(int ftkind, int costume)
{
    if (ftkind < 0) {
        return;
    }
    ftData_8008572C(ftkind);
    efAsync_LoadSync(ftData_UnkBytePerCharacter[ftkind]);
    ftData_80085820(ftkind, tag_clamp_costume(ftkind, costume));
    ftData_80085A14(ftkind);
}

/*
 * Before frame 1, run every lazy initialisation the engine would otherwise
 * do on first use (archive parsing, effect banks, Kirby copy data), so the
 * first switch costs only the fighter object, and so no load can happen in
 * a frame Slippi might roll back.  Files are waited for here if the
 * background loader has not finished them yet.  Sets bit r*3+k of
 * plan_avail for every entry that is fully in memory.
 */
void tag_preload_lazy_init(TagMatch* m)
{
    u8 kirby[TAG_PORTS * TAG_SLOTS];
    u8 chars[TAG_PORTS * TAG_SLOTS * 2];
    int nkirby = 0, nchars = 0;
    int r, k, i, j;

    tag_g.plan_avail = 0;
    if (!tag_g.plan_valid) {
        return;
    }
    /* Characters already in the match (vanilla loaded them). */
    for (i = 0; i < TAG_PORTS; i++) {
        if (Player_GetPlayerSlotType(i) == Gm_PKind_NA) {
            continue;
        }
        chars[nchars++] = (u8) Player_GetPlayerCharacter(i);
        if (Player_GetPlayerCharacter(i) == TAG_CKIND_KIRBY) {
            kirby[nkirby++] = (u8) Player_GetCostumeId(i);
        }
    }
    for (r = 0; r < tag_g.plan_rosters; r++) {
        for (k = 0; k < 3; k++) {
            int ck = tag_g.plan_roster[r].ckind[k];
            int co = tag_g.plan_roster[r].costume[k];
            if (!tag_g.plan_admit[r][k] || !tag_g.sound_admit[r][k] ||
                !tag_preload_char_ready(ck, co))
            {
                continue;
            }
            tag_load_kind(tag_ft_mapping[ck].internal_id, co);
            tag_load_kind(tag_ft_mapping[ck].extra_internal_id, co);
            tag_g.plan_avail |= (u16) (1u << (r * 3 + k));
            if (!tag_has(chars, nchars, ck)) {
                chars[nchars++] = (u8) ck;
            }
            if (ck == TAG_CKIND_KIRBY && !tag_has(kirby, nkirby, co)) {
                kirby[nkirby++] = (u8) co;
            }
        }
    }
    /*
     * Kirby can copy anyone in the match: every (character, Kirby costume)
     * pair needs its copy data and hat.  A pair that is missing makes the
     * extra that introduced it unavailable instead.
     */
    for (r = 0; r < tag_g.plan_rosters; r++) {
        for (k = 0; k < 3; k++) {
            int ck = tag_g.plan_roster[r].ckind[k];
            int co = tag_g.plan_roster[r].costume[k];
            int ok = 1;
            if (!(tag_g.plan_avail & (1u << (r * 3 + k)))) {
                continue;
            }
            for (j = 0; j < nkirby && ok; j++) {
                ok = tag_preload_kirby_ready(ck, kirby[j]);
            }
            if (ck == TAG_CKIND_KIRBY) {
                for (i = 0; i < nchars && ok; i++) {
                    ok = tag_preload_kirby_ready(chars[i], co);
                }
            }
            if (!ok) {
                tag_g.plan_avail &= (u16) ~(1u << (r * 3 + k));
            }
        }
    }
    for (j = 0; j < nkirby; j++) {
        for (i = 0; i < nchars; i++) {
            TagFtMapping* map = &tag_ft_mapping[chars[i]];
            if (!tag_preload_kirby_ready(chars[i], kirby[j])) {
                continue;
            }
            if (map->internal_id != Ft_Kind_Kirby) {
                Player_80031DA8(map->internal_id, kirby[j]);
            }
            if (map->extra_internal_id != -1) {
                Player_80031DA8(map->extra_internal_id, kirby[j]);
            }
        }
    }
    (void) m;
}
