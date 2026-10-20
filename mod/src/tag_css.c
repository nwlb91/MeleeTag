/*
 * Picking the roster on the character select screen.
 *
 * The vanilla CSS stays exactly as it is: the character a player picks the
 * normal way is slot Up (the primary).  The three other slots are assigned
 * with the C-stick, which the CSS does not use:
 *
 *   point the cursor at a character, flick C-stick Right/Down/Left
 *       -> that character goes to the D-pad slot of the same direction
 *   flick again in the same direction on the same character
 *       -> next costume (Zelda continues into Sheik's costumes and back)
 *   flick a direction while pointing at no character -> clear that slot
 *   flick C-stick Up -> clear all three extra slots
 *
 * Each door shows its roster as "R:FOX D:MAR L:SHK" (costume as ".n").
 * Rosters stay set across matches.  Online (Slippi), the one roster edited
 * on the online CSS is the local player's.
 */
#include <string.h>

#include "tag.h"
#include "tag_hook.h"

#define TAG_FLICK_ON 0.7F
#define TAG_FLICK_OFF 0.3F
#define TAG_CSS_TEXT_SIZE 0.045F
#define TAG_CSS_RESERVE 0x600
#define TAG_CSS_WIDTH 30

enum { TAG_FLICK_NONE = 0xFF };

static u8 tag_css_ready;

static u32 tag_css_sislib_free(void)
{
    SisBlock* b;
    u32 total = 0;

    for (b = free_head; b != NULL; b = b->next) {
        total += b->size;
    }
    return total;
}

static int tag_css_is_versus(void)
{
    u8 major = tag_scene_major();
    CSSData* css = mnCharSel_804D6CB0;

    if (major != TAG_MAJOR_VS && major != TAG_MAJOR_ONLINE) {
        return 0;
    }
    return css != NULL && css->match_type <= VS_SLOWMO;
}

/* The character icon under a cursor: where the coin would land. */
static int tag_css_hovered(struct TagCssCursor* cur)
{
    f32 x = cur->x + 2.7F;
    f32 y = cur->y - 2.0F;
    int i;

    for (i = 0; i < 25; i++) {
        CSSIcon* ic = &tag_css_icons[i];
        if (x > ic->bound_l && x < ic->bound_r && y < ic->bound_u &&
            y > ic->bound_d && ic->state >= 1)
        {
            return ic->char_kind;
        }
    }
    return -1;
}

/* Fresh flick direction as a slot index (0 Up .. 3 Left) or -1. */
static int tag_css_flick(int door, int port)
{
    HSD_PadStatus* ps = &HSD_PadCopyStatus[port];
    f32 cx = ps->nml_subStickX, cy = ps->nml_subStickY;
    f32 ax = cx < 0 ? -cx : cx, ay = cy < 0 ? -cy : cy;
    int dir = -1;

    if (ax < TAG_FLICK_OFF && ay < TAG_FLICK_OFF) {
        tag_g.css_prev_flick[door] = TAG_FLICK_NONE;
        return -1;
    }
    if (ax >= TAG_FLICK_ON || ay >= TAG_FLICK_ON) {
        if (ay >= ax) {
            dir = cy > 0 ? 0 : 2;
        } else {
            dir = cx > 0 ? 1 : 3;
        }
    }
    if (dir < 0 || tag_g.css_prev_flick[door] != TAG_FLICK_NONE) {
        return -1;
    }
    tag_g.css_prev_flick[door] = (u8) dir;
    return dir;
}

static void tag_css_assign(TagRosterExtras* r, int k, int ck)
{
    int cur = r->ckind[k];
    int co = r->costume[k];

    if (ck < 0) {
        r->ckind[k] = TAG_CKIND_NONE;
        r->costume[k] = 0;
        return;
    }
    if (ck == TAG_CKIND_ZELDA &&
        (cur == TAG_CKIND_ZELDA || cur == TAG_CKIND_SEAK))
    {
        /* Zelda's costumes, then Sheik's, then back to Zelda. */
        if (co + 1 < tag_num_costumes(cur)) {
            r->costume[k] = (u8) (co + 1);
        } else {
            r->ckind[k] = cur == TAG_CKIND_ZELDA ? TAG_CKIND_SEAK
                                                 : TAG_CKIND_ZELDA;
            r->costume[k] = 0;
        }
        return;
    }
    if (cur == ck) {
        r->costume[k] =
            (u8) tag_rules_next_costume(co, (u32) tag_num_costumes(ck));
        return;
    }
    r->ckind[k] = (u8) ck;
    r->costume[k] = 0;
}

static char* tag_css_put(char* o, const char* s)
{
    while (*s != '\0') {
        *o++ = *s++;
    }
    *o = '\0';
    return o;
}

static void tag_css_line(const TagRosterExtras* r, char* out)
{
    static const char* const dirs[3] = { "R:", "D:", "L:" };
    char* o = out;
    int k;

    *o = '\0';
    for (k = 0; k < 3; k++) {
        if (k != 0) {
            o = tag_css_put(o, " ");
        }
        o = tag_css_put(o, dirs[k]);
        if (!tag_ckind_valid(r->ckind[k])) {
            o = tag_css_put(o, "-");
            continue;
        }
        o = tag_css_put(o, tag_char_abbrev(r->ckind[k]));
        if (r->costume[k] != 0) {
            *o++ = '.';
            *o++ = (char) ('1' + r->costume[k]);
            *o = '\0';
        }
    }
}

static HSD_Text* tag_css_texts[TAG_PORTS];

static void tag_css_create_texts(int doors)
{
    static char blank[TAG_CSS_WIDTH + 1];
    int d;

    tag_css_ready = 1;
    memset(blank, ' ', TAG_CSS_WIDTH);
    blank[TAG_CSS_WIDTH] = '\0';
    for (d = 0; d < TAG_PORTS; d++) {
        HSD_Text* t;
        tag_css_texts[d] = NULL;
        if (d >= doors || tag_css_sislib_free() < TAG_CSS_RESERVE) {
            continue;
        }
        /* Canvas 0 is the CSS camera's (mnCharSel_Scene_OnEnter). */
        t = HSD_SisLib_803A6754(0, 0);
        if (t == NULL) {
            continue;
        }
        /* Under the icon rows, at the left edge of the door. */
        t->pos_x = -30.0F + 15.0F * d;
        t->pos_y = 1.8F;
        t->pos_z = 0.0F;
        t->font_size.x = TAG_CSS_TEXT_SIZE;
        t->font_size.y = TAG_CSS_TEXT_SIZE;
        t->default_kerning = 1;
        HSD_SisLib_803A6B98(t, 0.0F, 0.0F, "%s", blank);
        tag_css_texts[d] = t;
    }
}

static void tag_css_frame(void)
{
    int doors, d, changed = 0;
    int online = tag_scene_major() == TAG_MAJOR_ONLINE;

    if (!tag_css_is_versus()) {
        return;
    }
    doors = tag_css_door_count == 1 ? 1 : TAG_PORTS;
    if (!tag_css_ready) {
        tag_css_create_texts(doors);
    }
    for (d = 0; d < doors; d++) {
        struct TagCssCursor* cur = tag_css_cursor[d];
        TagRosterExtras* r = online ? &tag_g.net_local : &tag_g.css[d];
        char line[TAG_CSS_WIDTH + 8];
        int port, dir;

        if (cur == NULL) {
            continue;
        }
        port = doors == 1 ? tag_css_local_port : cur->port;
        if (port >= TAG_PORTS) {
            continue;
        }
        tag_g.css_human[d] = tag_css_doors.doors[d].p_kind == 0;
        if (HSD_PadCopyStatus[port].err == PAD_ERR_NONE &&
            (dir = tag_css_flick(d, port)) >= 0)
        {
            if (dir == 0) {
                tag_css_assign(r, 0, -1);
                tag_css_assign(r, 1, -1);
                tag_css_assign(r, 2, -1);
            } else {
                tag_css_assign(r, dir - 1, tag_css_hovered(cur));
            }
            changed = 1;
            lbAudioAx_800237A8(0xB7, 0x7F, 0x40);
        }
        if (tag_css_texts[d] != NULL) {
            tag_css_line(r, line);
            line[TAG_CSS_WIDTH] = '\0';
            HSD_SisLib_803A70A0(tag_css_texts[d], 0, "%s", line);
        }
    }
    if (changed) {
        tag_preload_request_refresh();
    }
}

TAG_TRAMPOLINE(tag_tramp_mnCharSel_Scene_OnEnter);

void tag_hook_mnCharSel_Scene_OnEnter(void* arg)
{
    int d;

    TAG_CALL_ORIG(tag_tramp_mnCharSel_Scene_OnEnter, void (*)(void*))(arg);
    /* The scene's text heap is new; texts are made on the first frame. */
    tag_css_ready = 0;
    for (d = 0; d < TAG_PORTS; d++) {
        tag_css_texts[d] = NULL;
        tag_g.css_prev_flick[d] = TAG_FLICK_NONE;
    }
}

TAG_TRAMPOLINE(tag_tramp_mnCharSel_Scene_OnFrame);

void tag_hook_mnCharSel_Scene_OnFrame(void)
{
    TAG_CALL_ORIG(tag_tramp_mnCharSel_Scene_OnFrame, void (*)(void))();
    tag_css_frame();
}
