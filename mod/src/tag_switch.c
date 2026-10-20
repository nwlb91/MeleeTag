/*
 * The switch itself, and the fighters that back each roster slot.
 *
 * Model.  A player always has exactly the fighters the engine expects in its
 * StaticPlayer: player_entity[0] is the active fighter and
 * player_entity[1] its partner (Nana, or the other Zelda/Sheik form), or
 * NULL.  The fighters of inactive slots are kept out of the StaticPlayer,
 * in the engine's Sleep state, the same state the engine keeps Sheik in
 * while Zelda plays.  Sleeping fighters have no hurtboxes, hitboxes,
 * camera box or update logic, so everything a character owns stays exactly
 * as it was while its slot is inactive.
 *
 * A switch mirrors the engine's Zelda/Sheik transform (ftCommon_8007EFC8)
 * field for field, with three deliberate differences: no transformation
 * flash or crowd reaction, momentum survives the state entry, and the
 * airtime counters go through the shared-airtime rules instead of being
 * copied.  Entering the incoming fighter's state goes through the engine's
 * own Enter functions, so physics, collision and state callbacks are rebuilt
 * by the engine.
 */
#include "tag.h"

/* tag_rules.c works on these numbers; keep them honest. */
_Static_assert(TAG_MS_SLEEP == ftCo_MS_Sleep, "Sleep");
_Static_assert(TAG_MS_REBIRTH == ftCo_MS_Rebirth, "Rebirth");
_Static_assert(TAG_MS_REBIRTHWAIT == ftCo_MS_RebirthWait, "RebirthWait");
_Static_assert(TAG_MS_WAIT == ftCo_MS_Wait, "Wait");
_Static_assert(TAG_MS_WALKSLOW == ftCo_MS_WalkSlow, "WalkSlow");
_Static_assert(TAG_MS_WALKMIDDLE == ftCo_MS_WalkMiddle, "WalkMiddle");
_Static_assert(TAG_MS_WALKFAST == ftCo_MS_WalkFast, "WalkFast");
_Static_assert(TAG_MS_TURN == ftCo_MS_Turn, "Turn");
_Static_assert(TAG_MS_RUN == ftCo_MS_Run, "Run");
_Static_assert(TAG_MS_JUMPF == ftCo_MS_JumpF, "JumpF");
_Static_assert(TAG_MS_JUMPB == ftCo_MS_JumpB, "JumpB");
_Static_assert(TAG_MS_JUMPAERIALF == ftCo_MS_JumpAerialF, "JumpAerialF");
_Static_assert(TAG_MS_JUMPAERIALB == ftCo_MS_JumpAerialB, "JumpAerialB");
_Static_assert(TAG_MS_FALL == ftCo_MS_Fall, "Fall");
_Static_assert(TAG_MS_FALLAERIALB == ftCo_MS_FallAerialB, "FallAerialB");
_Static_assert(TAG_MS_DAMAGEFALL == ftCo_MS_DamageFall, "DamageFall");
_Static_assert(TAG_MS_SQUAT == ftCo_MS_Squat, "Squat");
_Static_assert(TAG_MS_SQUATWAIT == ftCo_MS_SquatWait, "SquatWait");
_Static_assert(TAG_MS_SQUATRV == ftCo_MS_SquatRv, "SquatRv");
_Static_assert(TAG_MS_LANDING == ftCo_MS_Landing, "Landing");
_Static_assert(TAG_MS_LANDINGFALLSPECIAL == ftCo_MS_LandingFallSpecial,
               "LandingFallSpecial");
_Static_assert(TAG_MS_ATTACK11 == ftCo_MS_Attack11, "Attack11");
_Static_assert(TAG_MS_ATTACK13 == ftCo_MS_Attack13, "Attack13");
_Static_assert(TAG_MS_ATTACK100END == ftCo_MS_Attack100End, "Attack100End");
_Static_assert(TAG_MS_ATTACKDASH == ftCo_MS_AttackDash, "AttackDash");
_Static_assert(TAG_MS_ATTACKS3HI == ftCo_MS_AttackS3Hi, "AttackS3Hi");
_Static_assert(TAG_MS_ATTACKLW4 == ftCo_MS_AttackLw4, "AttackLw4");
_Static_assert(TAG_MS_ATTACKAIRN == ftCo_MS_AttackAirN, "AttackAirN");
_Static_assert(TAG_MS_ATTACKAIRLW == ftCo_MS_AttackAirLw, "AttackAirLw");
_Static_assert(TAG_MS_DAMAGEHI1 == ftCo_MS_DamageHi1, "DamageHi1");
_Static_assert(TAG_MS_DAMAGELW3 == ftCo_MS_DamageLw3, "DamageLw3");
_Static_assert(TAG_MS_DAMAGEAIR1 == ftCo_MS_DamageAir1, "DamageAir1");
_Static_assert(TAG_MS_DAMAGEAIR3 == ftCo_MS_DamageAir3, "DamageAir3");
_Static_assert(TAG_MS_DAMAGEFLYHI == ftCo_MS_DamageFlyHi, "DamageFlyHi");
_Static_assert(TAG_MS_DAMAGEFLYROLL == ftCo_MS_DamageFlyRoll, "DamageFlyRoll");
_Static_assert(TAG_MS_OTTOTTO == ftCo_MS_Ottotto, "Ottotto");
_Static_assert(TAG_MS_OTTOTTOWAIT == ftCo_MS_OttottoWait, "OttottoWait");
_Static_assert(TAG_MS_COMMON_COUNT == ftCo_MS_Count, "Count");
_Static_assert(sizeof(TagFtMapping) == 3, "ftMapping");

/* Fighters a switch may hold above the cap for the instant of the swap. */
#define TAG_PEAK_EXTRA 2

static int tag_player_index(TagMatch* m, TagPlayer* p)
{
    return (int) (p - m->pl);
}

HSD_GObj* tag_slot_front(TagSlot* s)
{
    return s->inst[s->form];
}

static HSD_GObj* tag_slot_back(TagSlot* s)
{
    return s->inst[s->form ^ 1];
}

static int tag_is_nana(HSD_GObj* g)
{
    return g != NULL && GET_FIGHTER(g)->kind == Ft_Kind_Nana;
}

void tag_switch_view(HSD_GObj* gobj, TagFighterView* v)
{
    Fighter* fp = GET_FIGHTER(gobj);

    v->motion_id = fp->motion_id;
    v->airborne = fp->ground_or_air == GA_Air;
    v->in_hitlag = fp->x2219_b5;
    v->in_hitstun = fp->x221C_b6;
    v->allow_interrupt = fp->allow_interrupt;
    v->landing_allowed = fp->mv.co.landing.allow_interrupt;
    v->anim_frame = fp->cur_anim_frame;
    v->landing_lag = fp->co_attrs.normal_landing_lag;
    v->sleeping = fp->is_sleeping;
    v->input_disabled = fp->input_disabled;
    v->grab_linked = fp->victim_gobj != NULL || fp->x1A5C != NULL;
    v->holding_hammer = ftCo_800C5240(gobj);
}

/* Entry state for the incoming fighter, or TAG_ENTRY_NONE. */
int tag_switch_entry(TagPlayer* p)
{
    TagSlot* s = &p->slot[p->active];
    HSD_GObj* front = tag_slot_front(s);
    HSD_GObj* back = tag_slot_back(s);
    TagFighterView v;
    int entry;

    if (front == NULL) {
        return TAG_ENTRY_NONE;
    }
    tag_switch_view(front, &v);
    entry = tag_rules_entry(&v);
    if (entry != TAG_ENTRY_NONE && tag_is_nana(back)) {
        tag_switch_view(back, &v);
        if (!tag_rules_partner_safe(&v)) {
            return TAG_ENTRY_NONE;
        }
    }
    return entry;
}

/* ---- Fighter objects ---------------------------------------------------- */

int tag_fighter_count(void)
{
    HSD_GObj* g;
    int n = 0;

    for (g = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER]; g != NULL; g = g->next) {
        n++;
    }
    return n;
}

static int tag_owns_items(HSD_GObj* fighter)
{
    HSD_GObj* g;

    if (fighter == NULL) {
        return 0;
    }
    for (g = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_ITEM]; g != NULL; g = g->next) {
        if (GET_ITEM(g)->owner == fighter) {
            return 1;
        }
    }
    return 0;
}

/*
 * Fighter_Create for a fighter that starts asleep, the way the engine
 * creates Sheik for a Zelda player.  It reads the costume from the
 * StaticPlayer, so that is lent for the call.
 */
static HSD_GObj* tag_create_fighter(int pi, int ftkind, int costume)
{
    StaticPlayer* sp = &tag_player_slots[pi];
    struct plAllocInfo info;
    u8 costume_save = sp->costume_id;
    HSD_GObj* g;

    sp->costume_id = (u8) costume;
    info.internal_id = ftkind;
    info.slot = (u8) pi;
    info.x5 = -1;
    *(u8*) &info.x6 = 0;
    info.x6.b0 = true;                 /* not the player's main fighter */
    info.x6.has_transformation = true; /* enter Sleep immediately */
    g = Fighter_Create(&info);
    sp->costume_id = costume_save;
    return g;
}

/*
 * HSD_GObjFree runs Fighter_Unload_8006DABC, which detaches every item the
 * fighter owns (it_8026B7F8) and then clears the StaticPlayer entity this
 * fighter would occupy if it were active (Player_80031FB0).  An inactive
 * slot's fighter is not in the StaticPlayer, so that last write is undone.
 */
static void tag_destroy_fighter(int pi, HSD_GObj* g)
{
    StaticPlayer* sp = &tag_player_slots[pi];
    HSD_GObj* e0 = sp->player_entity[0];
    HSD_GObj* e1 = sp->player_entity[1];
    enum_t state = sp->player_state;

    HSD_GObjFree(g);
    sp->player_entity[0] = e0;
    sp->player_entity[1] = e1;
    sp->player_state = state;
}

static void tag_evict_slot(TagMatch* m, int pi, TagSlot* s)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (s->inst[i] != NULL) {
            tag_persist_save(s->inst[i], &s->saved[i]);
        } else {
            s->saved[i].kind = 0xFF;
        }
    }
    s->saved_valid = 1;
    for (i = 0; i < 2; i++) {
        if (s->inst[i] != NULL) {
            tag_destroy_fighter(pi, s->inst[i]);
            s->inst[i] = NULL;
        }
    }
    (void) m;
}

/*
 * Free the least recently used inactive slot, preferring ones whose fighters
 * have no projectile still in flight (those keep their owner for kill
 * credit).  Ties go to the lower player and slot index.
 */
static int tag_evict_one(TagMatch* m, TagPlayer* keep_p, int keep_slot)
{
    int best_pi = -1, best_slot = -1;
    int best_items = 2;
    u32 best_used = 0;
    int pi, k;

    for (pi = 0; pi < TAG_PORTS; pi++) {
        TagPlayer* p = &m->pl[pi];
        if (!p->enabled) {
            continue;
        }
        for (k = 0; k < TAG_SLOTS; k++) {
            TagSlot* s = &p->slot[k];
            int items;
            if (k == p->active || s->inst[0] == NULL ||
                (p == keep_p && k == keep_slot))
            {
                continue;
            }
            items = tag_owns_items(s->inst[0]) || tag_owns_items(s->inst[1]);
            if (best_pi < 0 || items < best_items ||
                (items == best_items && s->last_used < best_used))
            {
                best_pi = pi;
                best_slot = k;
                best_items = items;
                best_used = s->last_used;
            }
        }
    }
    if (best_pi < 0) {
        return 0;
    }
    tag_evict_slot(m, best_pi, &m->pl[best_pi].slot[best_slot]);
    return 1;
}

static int tag_slot_needs(TagSlot* s)
{
    return tag_ft_mapping[s->ckind].extra_internal_id != -1 ? 2 : 1;
}

/*
 * Give slot `k` its fighters if it has none.  `limit` is the fighter count
 * that may not be exceeded; other inactive slots are evicted to get there.
 */
int tag_switch_make_resident(TagMatch* m, TagPlayer* p, int k, int limit)
{
    TagSlot* s = &p->slot[k];
    TagFtMapping* map;
    int pi = tag_player_index(m, p);
    int i;

    if (s->inst[0] != NULL) {
        return 1;
    }
    if (s->state != TAG_SLOT_READY) {
        return 0;
    }
    while (tag_fighter_count() + tag_slot_needs(s) > limit) {
        if (!tag_evict_one(m, p, k)) {
            return 0;
        }
    }
    map = &tag_ft_mapping[s->ckind];
    s->inst[0] = tag_create_fighter(pi, map->internal_id, s->costume);
    if (map->extra_internal_id != -1) {
        s->inst[1] = tag_create_fighter(pi, map->extra_internal_id, s->costume);
    }
    if (s->saved_valid) {
        for (i = 0; i < 2; i++) {
            if (s->inst[i] != NULL) {
                tag_persist_restore(s->inst[i], &s->saved[i]);
            }
        }
        s->saved_valid = 0;
    } else {
        /* Brand new fighters are already in their fresh-stock state. */
        s->partner_alive = 1;
        s->partner_damage = 0.0F;
        s->fresh = 0;
    }
    return 1;
}

/* The primary slot starts with the fighters the engine created. */
void tag_switch_adopt_primary(TagPlayer* p)
{
    TagSlot* s = &p->slot[TAG_SLOT_PRIMARY];
    StaticPlayer* sp = &tag_player_slots[p->port];
    HSD_GObj* front = sp->player_entity[sp->transformed[0]];
    HSD_GObj* back = sp->player_entity[sp->transformed[1]];
    TagFtMapping* map = &tag_ft_mapping[s->ckind];

    s->form = 0;
    s->inst[0] = front;
    s->inst[1] = back;
    if (back != NULL && GET_FIGHTER(back)->kind == (FighterKind) map->internal_id) {
        s->inst[0] = back;
        s->inst[1] = front;
        s->form = 1;
    }
    s->partner_alive = 1;
    s->partner_damage = 0.0F;
}

/* Zelda/Sheik: the engine's own transform swaps which form is in front. */
void tag_switch_sync_form(TagMatch* m, TagPlayer* p)
{
    TagSlot* s = &p->slot[p->active];
    HSD_GObj* front = Player_GetEntity(tag_player_index(m, p));

    if (s->inst[1] != NULL && front == s->inst[1]) {
        s->form = 1;
    } else if (front == s->inst[0]) {
        s->form = 0;
    }
}

/* ---- The transfer ------------------------------------------------------- */

static void tag_enter(HSD_GObj* g, int entry)
{
    switch (entry) {
    case TAG_ENTRY_SQUAT:
        ftCo_800D638C(g);
        break;
    case TAG_ENTRY_FALL:
        ftCo_Fall_Enter(g);
        break;
    case TAG_ENTRY_TUMBLE:
        ftCo_80090780(g);
        break;
    default:
        ft_8008A2BC(g);
        break;
    }
}

/*
 * Momentum survives the state entry exactly; from the next frame on the
 * incoming fighter's own friction, gravity and drift limits apply to it.
 */
static void tag_enter_keep_momentum(HSD_GObj* g, int entry)
{
    Fighter* fp = GET_FIGHTER(g);
    Vec3 self_vel = fp->self_vel;
    Vec3 kb_vel = fp->x8c_kb_vel;
    f32 gr_vel = fp->gr_vel;
    f32 gr_kb = fp->xF0_ground_kb_vel;
    u8 fast = fp->fall_fast;

    tag_enter(g, entry);
    fp->self_vel = self_vel;
    fp->x8c_kb_vel = kb_vel;
    fp->gr_vel = gr_vel;
    fp->xF0_ground_kb_vel = gr_kb;
    if (entry == TAG_ENTRY_FALL || entry == TAG_ENTRY_TUMBLE) {
        fp->fall_fast = fast;
    }
}

/*
 * ftCommon_8007EFC8 with src/dst explicit.  Shared player state moves from
 * the outgoing fighter to the incoming one; the outgoing fighter's
 * temporary action state ends with it going to sleep.
 */
static void tag_transfer(int pi, HSD_GObj* src_gobj, HSD_GObj* dst_gobj)
{
    Fighter* src = GET_FIGHTER(src_gobj);
    Fighter* dst = GET_FIGHTER(dst_gobj);

    Fighter_UnkInitReset_80067C98(dst);
    ftCommon_8007E2FC(dst_gobj);

    /* Position, facing, damage. */
    dst->cur_pos = src->cur_pos;
    dst->prev_pos = src->prev_pos;
    dst->pos_delta = src->pos_delta;
    dst->facing_dir = src->facing_dir;
    dst->dmg.x1830_percent = src->dmg.x1830_percent;
    Player_SetHPByIndex(pi, 0, dst->dmg.x1830_percent);
    dst->dmg.x18F0 = src->dmg.x18F0;

    /* Velocities and ground state. */
    dst->self_vel = src->self_vel;
    dst->ground_or_air = src->ground_or_air;
    dst->gr_vel = src->gr_vel;
    dst->xF0_ground_kb_vel = src->xF0_ground_kb_vel;
    dst->xF4_ground_attacker_shield_kb_vel =
        src->xF4_ground_attacker_shield_kb_vel;
    dst->xF8_playerNudgeVel.x = src->xF8_playerNudgeVel.x;
    dst->xF8_playerNudgeVel.y = src->xF8_playerNudgeVel.y;
    dst->x8c_kb_vel = src->x8c_kb_vel;           /* not in the transform */
    dst->x98_atk_shield_kb = src->x98_atk_shield_kb;
    dst->fall_fast = src->fall_fast;

    /* Controller history, so buffered inputs and smash detection carry on. */
    dst->input = src->input;
    dst->active_timer.lstick.x = src->active_timer.lstick.x;
    dst->active_timer.lstick.y = src->active_timer.lstick.y;
    dst->active_timer.trigger = src->active_timer.trigger;
    dst->active_sticky.lstick.x = src->active_sticky.lstick.x;
    dst->active_sticky.lstick.y = src->active_sticky.lstick.y;
    dst->active_sticky.trigger = src->active_sticky.trigger;
    dst->activity_timer.lstick.x = src->activity_timer.lstick.x;
    dst->activity_timer.lstick.y = src->activity_timer.lstick.y;
    dst->activity_timer.trigger = src->activity_timer.trigger;
    dst->x685 = src->x685;
    dst->x686 = src->x686;
    dst->x687 = src->x687;
    dst->x688 = src->x688;
    dst->x689 = src->x689;
    dst->x68A = src->x68A;
    dst->x68B = src->x68B;
    dst->x67C = src->x67C;
    dst->x67D = src->x67D;
    dst->x67E = src->x67E;
    dst->x681 = src->x681;
    dst->x682 = src->x682;
    dst->x67F = src->x67F;
    dst->x680 = src->x680;
    dst->x683 = src->x683;
    dst->x684 = src->x684;

    /* Who hit us last (kill credit), staling, restrictions. */
    dst->dmg.x18c4_source_ply = src->dmg.x18c4_source_ply;
    dst->dmg.x18C8 = src->dmg.x18C8;
    dst->x221F_b5 = src->x221F_b5;
    dst->dmg.x18CC = src->dmg.x18CC;
    dst->dmg.x18D0 = src->dmg.x18D0;
    dst->dmg.x18d4 = src->dmg.x18d4;
    dst->dmg.x18ec_instancehitby = src->dmg.x18ec_instancehitby;
    dst->dmg.x18ac_time_since_hit = src->dmg.x18ac_time_since_hit;
    dst->x21FC_flag = src->x21FC_flag;
    dst->x2221_b7 = src->x2221_b7;
    dst->x2221_b6 = src->x2221_b6;
    dst->x2221_b4 = src->x2221_b4;
    dst->x2221_b5 = src->x2221_b5;
    dst->x2104 = src->x2104;
    dst->dmg.x1910 = src->dmg.x1910;
    dst->x2064_ledgeCooldown = src->x2064_ledgeCooldown; /* ledge regrab */
    dst->x2098 = src->x2098;
    dst->capture_timer = src->capture_timer;

    /* Shared airtime (see tag_rules_map_jumps). */
    dst->x1968_jumpsUsed = (u8) tag_rules_map_jumps(
        src->ground_or_air == GA_Air, src->x1968_jumpsUsed,
        src->co_attrs.max_jumps, dst->co_attrs.max_jumps);
    dst->x1969_walljumpUsed = src->x1969_walljumpUsed;
    dst->used_tether = src->used_tether;
    dst->wall_jump_input_timer = src->wall_jump_input_timer;

    /* Shield, bury, stamina. */
    dst->shield_health = src->shield_health;
    dst->bury_timer_2 = src->bury_timer_2;
    dst->x2330 = src->x2330;
    dst->x2338 = src->x2338;
    dst->stamina_dead = src->stamina_dead;

    /* Timed intangibility (respawn, ledge) keeps its remaining frames. */
    if (src->x221D_b6) {
        ftColl_8007B7FC(dst, src->x2004);
        ftCo_800C0358(src, dst, 0x6B);
        ft_800880D8(src);
    }
    /* The transform's ftCo_800C0200(dst, 9) flash is left out. */
    if (src->x1990 != 0) {
        ftColl_8007B760(dst_gobj, src->x1990);
    }

    /* Item effects: star, mushrooms, metal box, bunny hood, flower. */
    dst->x2220_b5 = src->x2220_b5;
    dst->x2220_b6 = src->x2220_b6;
    dst->x2008 = src->x2008;
    ftLib_SetScale(dst_gobj, src->x34_scale.y);
    if (src->is_metal) {
        ftCo_800C8348(dst_gobj, src->metal_timer, src->metal_health);
        ftCo_800C8540(src_gobj);
    }
    if (src->x2226_b4) {
        if (!src->x2226_b7) {
            ftCo_800C88D4(dst_gobj, src->x2030, 0);
        }
        ftCo_800C8A64(src_gobj);
    }
    dst->x221E_b4 = src->x221E_b4;
    if (src->x197C != NULL) {
        it_8026B9A8(src->x197C, dst_gobj, dst->ft_data->x8->x12);
        it_802950D4(src->x197C, false);
        ftCommon_8007F948(dst_gobj, src->x197C, src->x2014);
        ftCommon_8007FA00(src_gobj);
    } else {
        dst->x197C = NULL;
    }
    if (src->x1980 != NULL) {
        it_8026B9A8(src->x1980, dst_gobj, dst->ft_data->x8->x12);
        ftCommon_8007FE84(dst_gobj, src->x1980, src->x2018, src->x2024);
        ftCommon_8007FDA0(src_gobj);
    } else {
        dst->x1980 = NULL;
    }

    /* Attributes, camera, collision, held item. */
    ftCo_800D105C(dst_gobj);
    ftCamera_80076064(dst);
    ft_800849EC(src, dst);
    ft_80081C88(dst_gobj, src->x34_scale.y);
    if (src->item_gobj != NULL) {
        dst->item_gobj = src->item_gobj;
        dst->x221E_b3 = src->x221E_b3;
        ftpickupitem_80094818(dst_gobj, 1);
        it_8026B9A8(src->item_gobj, dst_gobj, dst->ft_data->x8->x10);
        src->item_gobj = NULL;
    }

    if (!src->input_disabled) {
        ftLib_EnableInput(dst_gobj);
    }
    ftCo_800BFD04(src_gobj);
}

/* ---- Ice Climbers partner ---------------------------------------------- */

/* Nana leaves play with the leader; her damage and life go with the slot. */
static void tag_partner_out(TagSlot* s, HSD_GObj* nana)
{
    Fighter* np = GET_FIGHTER(nana);

    if (np->is_sleeping) {
        s->partner_alive = 0;
        return;
    }
    s->partner_alive = 1;
    s->partner_damage = np->dmg.x1830_percent;
    if (np->item_gobj != NULL) {
        /* As on death: a partner never carries an item out of play. */
        Item_8026A8EC(np->item_gobj);
        np->item_gobj = NULL;
        pl_8003EA08(np->player_idx, np->is_sub_fighter);
        pl_8003EA74(np->player_idx, np->is_sub_fighter);
    }
    tag_persist_switch_out(nana);
    ftCo_800BFD04(nana);
}

/*
 * Nana comes back where the leader is, which is by construction a valid
 * spot on the same surface or in the same air, moving the same way.
 */
static void tag_partner_in(int pi, TagSlot* s, HSD_GObj* leader,
                           HSD_GObj* nana, int entry)
{
    Fighter* lp = GET_FIGHTER(leader);
    Fighter* np = GET_FIGHTER(nana);

    if (!s->partner_alive) {
        return; /* stays out until the next stock, as in vanilla */
    }
    Fighter_UnkInitReset_80067C98(np);
    ftCommon_8007E2FC(nana);
    np->cur_pos = lp->cur_pos;
    np->prev_pos = lp->prev_pos;
    np->facing_dir = lp->facing_dir;
    np->ground_or_air = lp->ground_or_air;
    np->self_vel = lp->self_vel;
    np->gr_vel = lp->gr_vel;
    np->dmg.x1830_percent = s->partner_damage;
    Player_SetHPByIndex(pi, 1, np->dmg.x1830_percent);
    np->x1968_jumpsUsed = lp->x1968_jumpsUsed;
    ft_800849EC(lp, np);
    ftCo_800D105C(nana);
    ftCamera_80076064(np);
    if (!lp->input_disabled) {
        ftLib_EnableInput(nana);
    }
    tag_enter_keep_momentum(nana,
                            entry == TAG_ENTRY_TUMBLE ? TAG_ENTRY_FALL : entry);
}

/* ---- Switch ------------------------------------------------------------- */

static void tag_apply_fresh(TagSlot* s)
{
    int i;

    if (!s->fresh) {
        return;
    }
    for (i = 0; i < 2; i++) {
        if (s->inst[i] != NULL) {
            tag_persist_fresh(s->inst[i]);
        }
    }
    s->partner_alive = 1;
    s->partner_damage = 0.0F;
    s->fresh = 0;
}

int tag_switch_perform(TagMatch* m, TagPlayer* p, int k, int entry)
{
    int pi = tag_player_index(m, p);
    StaticPlayer* sp = &tag_player_slots[pi];
    TagSlot* out = &p->slot[p->active];
    TagSlot* in = &p->slot[k];
    HSD_GObj *src, *src_back, *dst, *dst_back;
    Fighter* dfp;
    int live;

    if (!tag_switch_make_resident(
            m, p, k, (int) tag_g.cfg->max_fighters + TAG_PEAK_EXTRA))
    {
        return 0;
    }
    tag_apply_fresh(in);

    src = tag_slot_front(out);
    src_back = tag_slot_back(out);
    dst = tag_slot_front(in);
    dst_back = tag_slot_back(in);
    dfp = GET_FIGHTER(dst);
    live = !GET_FIGHTER(src)->input_disabled;

    /* Outgoing partner and the outgoing fighter's leftovers. */
    if (tag_is_nana(src_back)) {
        tag_partner_out(out, src_back);
    }
    tag_persist_switch_out(src);

    /* The incoming fighters take the player's entity slots. */
    sp->player_entity[0] = dst;
    sp->player_entity[1] = dst_back;
    sp->transformed[0] = 0;
    sp->transformed[1] = 1;
    sp->ckind = in->ckind;
    sp->costume_id = in->costume;
    sp->flags.b2 = dst_back != NULL;
    dfp->is_sub_fighter = false;
    if (dst_back != NULL) {
        GET_FIGHTER(dst_back)->is_sub_fighter = true;
    }
    GET_FIGHTER(src)->is_sub_fighter = true;

    tag_transfer(pi, src, dst);
    tag_enter_keep_momentum(dst, entry);
    if (dfp->ground_or_air == GA_Air) {
        tag_persist_set_boost(dst, p->boost_used);
    } else {
        tag_persist_set_boost(dst, 0);
    }
    tag_persist_after_entry(dst);

    if (tag_is_nana(dst_back)) {
        tag_partner_in(pi, in, dst, dst_back, entry);
    } else if (dst_back != NULL && live) {
        /*
         * The other Zelda/Sheik form: vanilla enables both of a player's
         * fighters at GO; ours was created later, so do it here or the
         * player could not control it after a down-B transform.
         */
        ftLib_EnableInput(dst_back);
    }

    out->last_used = m->frame;
    p->active = (u8) k;
    p->cooldown = (u16) tag_g.cfg->cooldown_frames;

    /* Back under the cap, keeping the slot that was just left if possible. */
    while (tag_fighter_count() > (int) tag_g.cfg->max_fighters) {
        if (!tag_evict_one(m, NULL, -1)) {
            break;
        }
    }
    return 1;
}

/* A stock was lost: every slot not on screen starts the next stock fresh. */
void tag_switch_on_stock_lost(TagMatch* m, TagPlayer* p)
{
    int k;

    for (k = 0; k < TAG_SLOTS; k++) {
        TagSlot* s = &p->slot[k];
        if (k == p->active || s->state != TAG_SLOT_READY) {
            continue;
        }
        if (s->inst[0] != NULL) {
            s->fresh = 1;
        } else {
            /* Not resident: recreate from scratch instead of from saves. */
            s->saved_valid = 0;
        }
    }
    (void) m;
}
