/*
 * Match lifetime and the per-frame manager.
 *
 * fn_8016E730 loads every versus match (local versus scene 0x0202 and
 * Slippi's online scene 0x0208 reuse it).  After it returns, the payload
 * finishes the extra characters' loading, creates the manager GObj and
 * hands it the TagMatch state.
 *
 * The manager's only proc runs at priority 0 from p_link 0, which puts it
 * ahead of every fighter proc in the same frame (fighters are p_link 8 and
 * their first proc is also priority 0), and after HSD_PadRenewStatus.  So a
 * switch made here is in place before the incoming fighter reads its input
 * for the frame: the switch costs no frame.  Fighters created during the
 * proc have their procs queued behind ours and run the same frame.
 */
#include "tag.h"
#include "tag_hook.h"

#define TAG_FEEDBACK_FRAMES 45

TagMatch* tag_match_current(void)
{
    return tag_g.match;
}

static int tag_fighters_paused(void)
{
    u64* mask = HSD_GObjLibInitData.unk_2;
    return mask != NULL &&
           ((u32) (*mask >> HSD_GOBJ_PLINK_FIGHTER) & 1) != 0;
}

static void tag_strip(HSD_PadStatus* gs, u32 bits)
{
    gs->button &= ~bits;
    gs->last_button &= ~bits;
    gs->trigger &= ~bits;
    gs->release &= ~bits;
    gs->repeat &= ~bits;
}

static void tag_set_slot(TagSlot* s, int ckind, int costume, int ready)
{
    s->ckind = (u8) ckind;
    s->costume = (u8) costume;
    s->fresh = 0;
    s->form = 0;
    s->partner_alive = 1;
    s->partner_damage = 0.0F;
    s->saved_valid = 0;
    s->last_used = 0;
    s->inst[0] = s->inst[1] = NULL;
    if (!tag_ckind_valid(ckind)) {
        s->state = TAG_SLOT_EMPTY;
    } else {
        s->state = ready ? TAG_SLOT_READY : TAG_SLOT_UNLOADED;
    }
}

/* Slots 1..3 of a player from plan roster `r`, availability from `avail`. */
static void tag_apply_roster(TagPlayer* p, int r, u16 avail)
{
    int k;

    for (k = 0; k < 3; k++) {
        if (r < 0) {
            tag_set_slot(&p->slot[k + 1], TAG_CKIND_NONE, 0, 0);
            continue;
        }
        tag_set_slot(&p->slot[k + 1], tag_g.plan_roster[r].ckind[k],
                     tag_g.plan_roster[r].costume[k],
                     (avail >> (r * 3 + k)) & 1);
    }
    p->enabled = r >= 0 && !tag_roster_empty(&tag_g.plan_roster[r]);
}

static void tag_player_start(TagMatch* m, int pi)
{
    TagPlayer* p = &m->pl[pi];
    int r;

    p->port = (u8) Player_GetPadPort(pi);
    p->active = TAG_SLOT_PRIMARY;
    p->falls = Player_GetFallsByIndex(pi, 0);
    p->cooldown = 0;
    p->prev_dpad = (u8) (HSD_PadMasterStatus[p->port].button & TAG_PAD_DPAD);
    tag_net_rx_reset(&p->rx);
    tag_set_slot(&p->slot[TAG_SLOT_PRIMARY], Player_GetPlayerCharacter(pi),
                 Player_GetCostumeId(pi), 1);
    tag_switch_adopt_primary(m, p);

    if (m->netplay) {
        /* Decided once every client has announced (tag_net.c). */
        tag_apply_roster(p, -1, 0);
        return;
    }
    for (r = 0; r < tag_g.plan_rosters; r++) {
        if (tag_g.plan_owner[r] == pi) {
            break;
        }
    }
    tag_apply_roster(p, r < tag_g.plan_rosters ? r : -1, tag_g.plan_avail);
}

/*
 * Fill free fighter capacity with sleeping fighters for roster slots, one
 * per frame, slot 1 of every player before any slot 2.  A switch to a slot
 * that already has its fighter is just the transfer.
 */
static void tag_precreate_step(TagMatch* m)
{
    int cap = (int) tag_g.cfg->max_fighters;

    while (m->create_cursor < 3 * TAG_PORTS) {
        int k = 1 + m->create_cursor / TAG_PORTS;
        int pi = m->create_cursor % TAG_PORTS;
        TagPlayer* p = &m->pl[pi];
        TagSlot* s = &p->slot[k];
        int needs;

        m->create_cursor++;
        if (!p->enabled || s->state != TAG_SLOT_READY || s->inst[0] != NULL) {
            continue;
        }
        needs = tag_ft_mapping[s->ckind].extra_internal_id != -1 ? 2 : 1;
        if (tag_fighter_count() + needs > cap) {
            continue;
        }
        tag_switch_make_resident(m, p, k, cap);
        return;
    }
}

static u32 tag_ready_mask(TagPlayer* p)
{
    u32 mask = 0;
    int k;

    for (k = 0; k < TAG_SLOTS; k++) {
        if (p->slot[k].state == TAG_SLOT_READY) {
            mask |= 1u << k;
        }
    }
    return mask;
}

static void tag_player_frame(TagMatch* m, TagPlayer* p)
{
    int pi = (int) (p - m->pl);
    HSD_PadStatus* ms = &HSD_PadMasterStatus[p->port];
    u32 held = ms->button & TAG_PAD_DPAD;
    int slot = tag_rules_fresh_slot(p->prev_dpad, held);
    HSD_GObj* front;
    s32 falls;

    p->prev_dpad = (u8) held;
    /* The D-pad is ours: no taunts or other D-pad actions for this player. */
    tag_strip(&HSD_PadGameStatus[p->port], TAG_PAD_DPAD);

    if (m->phase != TAG_PHASE_ACTIVE) {
        return;
    }
    p->cooldown = (u16) tag_rules_tick(p->cooldown);
    if (p->feedback_timer != 0) {
        p->feedback_timer--;
    }

    /*
     * Every KO of the main fighter (stock or time match) bumps falls[0]
     * (ftCo_800D34E0); Nana's own KOs count in falls[1].
     */
    falls = Player_GetFallsByIndex(pi, 0);
    if (falls != p->falls) {
        tag_switch_on_stock_lost(m, p);
    }
    p->falls = falls;

    tag_switch_sync_form(m, p);
    front = tag_slot_front(&p->slot[p->active]);
    if (front == NULL) {
        return;
    }
    if (GET_FIGHTER(front)->ground_or_air == GA_Ground) {
        p->boost_used = 0;
    } else if (tag_persist_boost_used(front)) {
        p->boost_used = 1;
    }

    if (slot >= 0) {
        TagRequest req;
        int entry = tag_switch_entry(p);
        int d;

        req.slot = slot;
        req.active = p->active;
        req.ready_mask = tag_ready_mask(p);
        req.cooldown = p->cooldown;
        req.actionable = entry != TAG_ENTRY_NONE;
        d = tag_rules_decide(&req);
        if (d == TAG_DECIDE_SWITCH && !tag_switch_perform(m, p, slot, entry)) {
            d = TAG_DECIDE_EMPTY; /* no room for the fighter this instant */
        }
        if (d != TAG_DECIDE_NONE && d != TAG_DECIDE_SAME) {
            p->feedback = (u8) d;
            p->feedback_timer = TAG_FEEDBACK_FRAMES;
        }
    }
}

static void tag_match_proc(HSD_GObj* gobj)
{
    TagMatch* m = gobj->user_data;
    int pi;

    if (m == NULL || m->magic != TAG_MATCH_MAGIC || tag_fighters_paused()) {
        return;
    }
    m->frame++;
    if (m->netplay) {
        tag_net_frame(m);
    }
    for (pi = 0; pi < TAG_PORTS; pi++) {
        TagPlayer* p = &m->pl[pi];
        if (p->enabled || (m->netplay && m->phase == TAG_PHASE_ROSTERS &&
                           Player_GetPlayerSlotType(pi) == Gm_PKind_Human))
        {
            tag_player_frame(m, p);
        }
    }
    if (m->phase == TAG_PHASE_ACTIVE) {
        tag_precreate_step(m);
    }
    tag_hud_update(m);
}

static void tag_match_free(void* data)
{
    TagMatch* m = data;

    if (tag_g.match == m) {
        tag_g.match = NULL;
    }
    tag_net_stop();
    m->magic = 0;
    HSD_Free(m);
}

static void tag_match_start(void)
{
    TagMatch* m;
    HSD_GObj* gobj;
    int pi;

    m = HSD_MemAlloc(sizeof(TagMatch));
    if (m == NULL) {
        return;
    }
    memzero(m, sizeof(TagMatch));
    m->magic = TAG_MATCH_MAGIC;
    m->netplay = tag_scene_major() == TAG_MAJOR_ONLINE;
    m->phase = m->netplay ? TAG_PHASE_ROSTERS : TAG_PHASE_ACTIVE;
    if (m->netplay && !(tag_g.cfg->flags & TAG_CFG_NETSYNC)) {
        m->phase = TAG_PHASE_OFF;
    }

    tag_preload_lazy_init(m);
    for (pi = 0; pi < TAG_PORTS; pi++) {
        if (Player_GetPlayerSlotType(pi) == Gm_PKind_Human) {
            tag_player_start(m, pi);
        }
    }

    gobj = GObj_Create(14, 0, 0);
    if (gobj == NULL) {
        HSD_Free(m);
        return;
    }
    GObj_InitUserData(gobj, 0, tag_match_free, m);
    HSD_GObj_SetupProc(gobj, tag_match_proc, 0);
    m->gobj = gobj;
    tag_g.match = m;

    if (tag_g.cfg->flags & TAG_CFG_HUD) {
        tag_hud_create(m);
    }
    if (m->netplay && m->phase == TAG_PHASE_ROSTERS) {
        tag_net_prepare(m);
    }
}

TAG_TRAMPOLINE(tag_tramp_fn_8016E730);

void tag_hook_fn_8016E730(StartMeleeData* data)
{
    u8 major = tag_scene_major();
    int ours = (major == TAG_MAJOR_VS || major == TAG_MAJOR_ONLINE) &&
               tag_g.plan_valid;

    tag_g.match = NULL;
    tag_g.loading_match = (u8) ours;
    TAG_CALL_ORIG(tag_tramp_fn_8016E730, void (*)(StartMeleeData*))(data);
    tag_g.loading_match = 0;
    if (ours) {
        tag_match_start();
    }
}
