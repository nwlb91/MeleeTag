/*
 * In-match slot display: one line per tag player at the spot the vanilla
 * HUD uses for the end-of-match score (unused while the match runs):
 *
 *   FOX fco mar -     the active slot in capitals, then Right, Down, Left
 *   FOX fco mar - 12  cooldown frames left
 *   FOX fco mar - BUSY  last request refused: BUSY (not actionable),
 *                       WAIT (cooldown), N.A. (slot empty or not loaded)
 *
 * Text goes through the HUD's own camera (gx link 11, font 2) like the score
 * text does.  The in-match text heap is small (0x4800 bytes), so each line
 * is created at its full width once and only rewritten in place afterwards;
 * SisLib then never allocates again.  The last text written lives in the
 * TagMatch, so a rollback restores it together with the text object.
 */
#include <string.h>

#include "tag.h"

#define TAG_HUD_SCALE 0.045F
#define TAG_HUD_DY 3.2F
#define TAG_SISLIB_RESERVE 0x900

static u32 tag_sislib_free(void)
{
    SisBlock* b;
    u32 total = 0;

    for (b = free_head; b != NULL; b = b->next) {
        total += b->size;
    }
    return total;
}

static void tag_copy_abbrev(char* out, int ckind, int upper)
{
    const char* a = tag_char_abbrev(ckind);
    int i;

    for (i = 0; a[i] != '\0'; i++) {
        char c = a[i];
        if (!upper && c >= 'A' && c <= 'Z') {
            c = (char) (c - 'A' + 'a');
        }
        *out++ = c;
    }
    *out = '\0';
}

static char* tag_append(char* out, const char* s)
{
    while (*s != '\0') {
        *out++ = *s++;
    }
    *out = '\0';
    return out;
}

static char* tag_append_num(char* out, u32 v)
{
    char tmp[6];
    int n = 0;

    do {
        tmp[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v != 0 && n < 5);
    while (n > 0) {
        *out++ = tmp[--n];
    }
    *out = '\0';
    return out;
}

static void tag_hud_text(TagMatch* m, TagPlayer* p, char* out)
{
    char* o = out;
    int k;

    out[0] = '\0';
    if (m->phase == TAG_PHASE_ROSTERS) {
        tag_append(out, "TAG SYNC");
        return;
    }
    if (m->phase == TAG_PHASE_OFF || !p->enabled) {
        return;
    }
    for (k = 0; k < TAG_SLOTS; k++) {
        TagSlot* s = &p->slot[k];
        char name[8];
        if (k != 0) {
            o = tag_append(o, " ");
        }
        if (s->state != TAG_SLOT_READY) {
            o = tag_append(o, "-");
            continue;
        }
        tag_copy_abbrev(name, s->ckind, k == p->active);
        o = tag_append(o, name);
    }
    if (p->feedback_timer != 0) {
        switch (p->feedback) {
        case TAG_DECIDE_BUSY:
            tag_append(o, " BUSY");
            return;
        case TAG_DECIDE_COOLDOWN:
            tag_append(o, " WAIT");
            return;
        case TAG_DECIDE_EMPTY:
            tag_append(o, " N.A.");
            return;
        default:
            break;
        }
    }
    if (p->cooldown != 0) {
        o = tag_append(o, " ");
        tag_append_num(o, p->cooldown);
    }
}

void tag_hud_create(TagMatch* m)
{
    static char blank[TAG_HUD_WIDTH + 1];
    HSD_Text* t;
    int ctx, pi;

    m->hud_ready = 0;
    if (tag_sislib_free() < TAG_SISLIB_RESERVE || ifAll_GetHUDGObj() == NULL) {
        return;
    }
    memset(blank, ' ', TAG_HUD_WIDTH);
    blank[TAG_HUD_WIDTH] = '\0';
    ctx = HSD_SisLib_803A611C(2, ifAll_GetHUDGObj(), 14, 15, 0, 11, 0, 19);
    t = HSD_SisLib_803A6754(2, ctx);
    if (t == NULL) {
        return;
    }
    t->default_alignment = 1;
    t->default_kerning = 1;
    for (pi = 0; pi < TAG_PORTS; pi++) {
        Vec3* pos;
        m->hud_line[pi] = -1;
        if (Player_GetPlayerSlotType(pi) != Gm_PKind_Human) {
            continue;
        }
        pos = ifAll_GetPlayerHUDPosition(pi);
        m->hud_line[pi] =
            HSD_SisLib_803A6B98(t, pos->x, TAG_HUD_DY + pos->y, "%s", blank);
        HSD_SisLib_803A7548(t, m->hud_line[pi], TAG_HUD_SCALE, TAG_HUD_SCALE);
        m->hud_last[pi][0] = '\1'; /* force the first write */
    }
    m->hud = t;
    m->hud_ready = 1;
}

void tag_hud_update(TagMatch* m)
{
    char line[TAG_HUD_WIDTH + 8];
    int pi;

    if (!m->hud_ready) {
        return;
    }
    for (pi = 0; pi < TAG_PORTS; pi++) {
        if (m->hud_line[pi] < 0) {
            continue;
        }
        tag_hud_text(m, &m->pl[pi], line);
        line[TAG_HUD_WIDTH] = '\0';
        if (strcmp(line, m->hud_last[pi]) == 0) {
            continue;
        }
        strcpy(m->hud_last[pi], line);
        HSD_SisLib_803A70A0(m->hud, m->hud_line[pi], "%s", line);
    }
}
