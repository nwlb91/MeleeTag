/*
 * Per-character state that belongs to a roster slot rather than to the
 * player or the current action.
 *
 * While a slot's fighter stays resident (asleep when not active) all of its
 * character state simply stays in that fighter; the engine's sleep state
 * runs none of the character's update code, so nothing regenerates or
 * decays while a slot is inactive.  The table below is what a slot must
 * remember when its fighter is destroyed to make room for another and later
 * created again.  It was built by auditing every FighterVars field of the
 * 26 playable characters in the decomp (fighter vars at fp+222C):
 *
 *   kept per slot            reset by the character's OnDeath on a new stock
 *   ------------------------------------------------------------------------
 *   DK punch swings          dk.x222C, dk.x2230
 *   Samus charge             ss.x2230
 *   Mewtwo shadow ball       mt.x2234
 *   Sheik needles            sk.x0
 *   Bowser fire fuel         kp.x222C, kp.x2230 (refills only while active)
 *   Mario/Doc pill colours   mr.x222C, mr.x2230
 *   G&W judge/chef/bucket    gw.x222C..x2244 (bucket fill survives a stock)
 *   Peach smash item cycle   pe.attacks4_motion_id
 *   Kirby copy ability       kb.hat.kind plus the copied resources
 *
 * Everything else in those structs is an item/effect pointer, a per-action
 * timer, or a once-per-airtime flag (handled by the boost token below).
 */
#include "tag.h"

#define TAG_OFF(field) ((u16) __builtin_offsetof(Fighter, field))

typedef struct TagField {
    u8 kind;
    u8 size;
    u16 off;
} TagField;

static const TagField tag_persist_fields[] = {
    { Ft_Kind_Donkey, 4, TAG_OFF(u.dk.x222C) },
    { Ft_Kind_Donkey, 4, TAG_OFF(u.dk.x2230) },
    { Ft_Kind_Samus, 4, TAG_OFF(u.ss.x2230) },
    { Ft_Kind_Mewtwo, 4, TAG_OFF(u.mt.x2234_shadowBallCharge) },
    { Ft_Kind_Seak, 4, TAG_OFF(u.sk.x0) },
    { Ft_Kind_Koopa, 4, TAG_OFF(u.kp.x222C) },
    { Ft_Kind_Koopa, 4, TAG_OFF(u.kp.x2230) },
    { Ft_Kind_Mario, 4, TAG_OFF(u.mr.x222C_vitaminCurr) },
    { Ft_Kind_Mario, 4, TAG_OFF(u.mr.x2230_vitaminPrev) },
    { Ft_Kind_DrMario, 4, TAG_OFF(u.mr.x222C_vitaminCurr) },
    { Ft_Kind_DrMario, 4, TAG_OFF(u.mr.x2230_vitaminPrev) },
    { Ft_Kind_GameWatch, 4, TAG_OFF(u.gw.x222C_judgeVar1) },
    { Ft_Kind_GameWatch, 4, TAG_OFF(u.gw.x2230_judgeVar2) },
    { Ft_Kind_GameWatch, 4, TAG_OFF(u.gw.x2238_panicCharge) },
    { Ft_Kind_GameWatch, 4, TAG_OFF(u.gw.x223C_panicDamage) },
    { Ft_Kind_GameWatch, 4, TAG_OFF(u.gw.x2240_chefVar1) },
    { Ft_Kind_GameWatch, 4, TAG_OFF(u.gw.x2244_chefVar2) },
    { Ft_Kind_Peach, 4, TAG_OFF(u.pe.attacks4_motion_id) },
    /* Kirby: the hat itself is handled in code; these are the resources of
     * copied abilities (Bowser fuel, Samus/Mewtwo/DK charges, ...). */
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.x68) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.x6C) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.x84) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.x88) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.x9C) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.xA8) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.xB4) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.xBC) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.xD4) },
    { Ft_Kind_Kirby, 4, TAG_OFF(u.kb.xD8) },
};

/*
 * Once-per-airtime boosts.  The engine clears each of these when the
 * fighter lands (ftCo_Landing_Enter and the characters' landing code).  A
 * player gets one boost per airtime no matter how many characters take part
 * in it: tag_match.c keeps a shared token, set as soon as the active fighter
 * consumes any boost and cleared on landing, and each incoming fighter's
 * flags are written from that token.
 */
typedef struct TagBoost {
    u8 kind;
    u8 consumed; /* value meaning "already used this airtime" */
    u16 off;
} TagBoost;

static const TagBoost tag_boosts[] = {
    { Ft_Kind_Mario, 1, TAG_OFF(u.mr.x2234_tornadoCharge) },
    { Ft_Kind_Mario, 1, TAG_OFF(u.mr.x2238_isCapeBoost) },
    { Ft_Kind_DrMario, 1, TAG_OFF(u.mr.x2234_tornadoCharge) },
    { Ft_Kind_DrMario, 1, TAG_OFF(u.mr.x2238_isCapeBoost) },
    { Ft_Kind_Peach, 0, TAG_OFF(u.pe.has_float) },
    { Ft_Kind_Peach, 1, TAG_OFF(u.pe.specialairn_used) },
    { Ft_Kind_Mars, 1, TAG_OFF(u.ms.x222C) },
    { Ft_Kind_Emblem, 1, TAG_OFF(u.ms.x222C) },
    { Ft_Kind_GameWatch, 1, TAG_OFF(u.gw.x2234) },
    { Ft_Kind_Popo, 1, TAG_OFF(u.pp.x224C) },
    { Ft_Kind_Nana, 1, TAG_OFF(u.nn.x224C) },
    { Ft_Kind_Kirby, 1, TAG_OFF(u.kb.x64) },
    { Ft_Kind_Kirby, 1, TAG_OFF(u.kb.xC4) },
    { Ft_Kind_Kirby, 1, TAG_OFF(u.kb.xCC) },
    { Ft_Kind_Mewtwo, 1, TAG_OFF(u.mt.x223C_isConfusionBoost) },
    { Ft_Kind_Luigi, 1, TAG_OFF(u.lg.x222C_cycloneCharge) },
};

#define TAG_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static u32 tag_read(Fighter* fp, u16 off, int size)
{
    u8* p = (u8*) fp + off;
    return size == 1 ? *p : *(u32*) p;
}

static void tag_write(Fighter* fp, u16 off, int size, u32 v)
{
    u8* p = (u8*) fp + off;
    if (size == 1) {
        *p = (u8) v;
    } else {
        *(u32*) p = v;
    }
}

void tag_persist_save(HSD_GObj* gobj, TagSaved* out)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned i;
    int n = 0;

    out->kind = (u8) fp->kind;
    for (i = 0; i < TAG_COUNT(tag_persist_fields); i++) {
        const TagField* f = &tag_persist_fields[i];
        if (f->kind == fp->kind && n < TAG_PERSIST_MAX - 1) {
            out->v[n++] = tag_read(fp, f->off, f->size);
        }
    }
    if (fp->kind == Ft_Kind_Kirby) {
        out->v[n++] = (u32) fp->u.kb.hat.kind;
    }
    out->count = (u8) n;
}

void tag_persist_restore(HSD_GObj* gobj, const TagSaved* in)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned i;
    int n = 0;

    if (in->kind != fp->kind) {
        return;
    }
    if (fp->kind == Ft_Kind_Kirby && in->count > 0) {
        /*
         * Grant first: the ability's init callback sets up its own
         * resources, which the saved values then overwrite.
         */
        s32 hat = (s32) in->v[in->count - 1];
        if (hat != Ft_Kind_Kirby && hat >= 0 && hat < Ft_Kind_Max) {
            ftKb_SpecialN_800F1BAC(gobj, hat, false);
        }
    }
    for (i = 0; i < TAG_COUNT(tag_persist_fields); i++) {
        const TagField* f = &tag_persist_fields[i];
        if (f->kind == fp->kind && n < in->count) {
            tag_write(fp, f->off, f->size, in->v[n++]);
        }
    }
}

/*
 * The engine's own fresh-stock reset for a fighter that sat out the stock:
 * the cleanup callbacks it would have run on dying, then the character's
 * OnDeath, which is what Fighter_Spawn runs on a respawn.
 */
void tag_persist_fresh(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObjEvent cb;

    if ((cb = fp->death3_cb) != NULL) {
        fp->death3_cb = NULL;
        cb(gobj);
    }
    if ((cb = fp->death1_cb) != NULL) {
        fp->death1_cb = NULL;
        cb(gobj);
    }
    if (ftData_OnDeath[fp->kind] != NULL) {
        ftData_OnDeath[fp->kind](gobj);
    }
}

int tag_persist_boost_used(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned i;

    for (i = 0; i < TAG_COUNT(tag_boosts); i++) {
        const TagBoost* b = &tag_boosts[i];
        if (b->kind == fp->kind &&
            (tag_read(fp, b->off, 4) != 0) == (b->consumed != 0))
        {
            return 1;
        }
    }
    return 0;
}

void tag_persist_set_boost(HSD_GObj* gobj, int used)
{
    Fighter* fp = GET_FIGHTER(gobj);
    unsigned i;

    for (i = 0; i < TAG_COUNT(tag_boosts); i++) {
        const TagBoost* b = &tag_boosts[i];
        if (b->kind == fp->kind) {
            u32 v = used ? b->consumed : !b->consumed;
            tag_write(fp, b->off, 4, v);
        }
    }
}

/*
 * Leaving play.  The death path runs a fighter's death1/death3 callbacks to
 * detach whatever its earlier actions left behind that still calls back into
 * it (Link's returning boomerang, Peach's parasol, Popo's belay, Mewtwo's
 * held ball, tether hooks).  Fired projectiles have no such callback and keep
 * flying with this fighter as owner, so hits still credit the right player.
 * Kirby's callbacks would strip his copied ability, which belongs to the
 * slot, so Kirby keeps them until his next fresh-stock reset.
 */
void tag_persist_switch_out(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);
    HSD_GObjEvent cb;

    if (fp->kind != Ft_Kind_Kirby) {
        if ((cb = fp->death3_cb) != NULL) {
            fp->death3_cb = NULL;
            cb(gobj);
        }
        if ((cb = fp->death1_cb) != NULL) {
            fp->death1_cb = NULL;
            cb(gobj);
        }
    }
    /* Looping and voice sounds, as on death. */
    ft_80088C5C(gobj);
}

/*
 * After the incoming fighter has entered its state.  Fighter_UnkInitReset
 * clears death1/death3; a Kirby wearing a hat re-arms them exactly as
 * ftKb_SpecialN_800F1BAC does when he copies (same kind: no model change).
 */
void tag_persist_after_entry(HSD_GObj* gobj)
{
    Fighter* fp = GET_FIGHTER(gobj);

    if (fp->kind == Ft_Kind_Kirby && fp->u.kb.hat.kind != Ft_Kind_Kirby) {
        ftKb_SpecialN_800F1BAC(gobj, fp->u.kb.hat.kind, false);
    }
}
