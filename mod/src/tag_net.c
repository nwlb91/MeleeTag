/*
 * Slippi netplay.
 *
 * Slippi sends each client's controller state to the others every frame and
 * runs the game in lockstep with rollback, so anything that changes the
 * match must be decided from those inputs alone.  Rosters are picked on
 * each client's own CSS, and Slippi gives the mod no channel before the
 * match, so they travel inside the inputs: PADStatus.button bits 0x4000
 * and 0x8000 are never set by a controller, Slippi transmits the whole
 * button word, and the manager strips the bits again before any game code
 * reads them (tag_rules.h has the framing).
 *
 * The bits are added in PADRead, whose result Slippi's input hook (right
 * after the PADRead call in HSD_PadRenewRawStatus) sends as this client's
 * input, so they reach every client as part of its inputs for that frame.  Decoding runs inside the
 * simulation on confirmed inputs, so every client decides on the same
 * frame, and a rollback replays the decision.
 *
 * Characters cannot be loaded once the match runs, so a client can only
 * preload the opponents' rosters it learned in the previous match.  Tag is
 * enabled when every client preloaded exactly the rosters announced; each
 * slot is enabled when every client reports it fully loaded.  Otherwise the
 * match is played with primary characters only, and the next match (same
 * opponents, same rosters) has everything preloaded.
 */
#include "tag.h"
#include "tag_hook.h"

#define TAG_NET_DEADLINE 480

void tag_net_prepare(TagMatch* m)
{
    TagAnnounce a;
    int k;

    a.version = TAG_NET_VERSION;
    a.config = tag_config_hash();
    for (k = 0; k < 3; k++) {
        a.ckind[k] = tag_g.net_local.ckind[k];
        a.costume[k] = tag_g.net_local.costume[k];
    }
    a.preload = tag_g.plan_hash;
    a.avail = tag_g.plan_avail;
    tag_net_encode(&a, tag_g.net_msg);
    tag_g.net_tx = 1;
    (void) m;
}

void tag_net_stop(void)
{
    tag_g.net_tx = 0;
}

TAG_TRAMPOLINE(tag_tramp_PADRead);

/*
 * The bit sent is chosen by the match's frame counter, not by how often
 * PADRead runs: Slippi's polling codes can read the pads more than once per
 * frame, and during a rollback the pads are not read at all.  The counter
 * advances once per simulated frame, so consecutive frames of input carry
 * consecutive bits whatever the polling pattern or input delay.
 */
u32 tag_hook_PADRead(PADStatus* status)
{
    u32 ret = TAG_CALL_ORIG(tag_tramp_PADRead, u32 (*)(PADStatus*))(status);
    TagMatch* m = tag_g.match;

    if (tag_g.net_tx && m != NULL && m->magic == TAG_MATCH_MAGIC &&
        tag_scene_major() == TAG_MAJOR_ONLINE &&
        tag_scene_minor() == TAG_MINOR_INGAME)
    {
        u32 bits = tag_net_tx_bits(tag_g.net_msg, m->frame % TAG_NET_BITS);
        int i;
        for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
            if (status[i].err == PAD_ERR_NONE) {
                status[i].button |= (u16) bits;
            }
        }
    }
    return ret;
}

/* Remember the opponents' rosters for the next match's preload. */
static void tag_learn(const TagAnnounce* ann, int n)
{
    int i, own = -1;

    tag_g.learned_count = 0;
    for (i = 0; i < n; i++) {
        TagRosterExtras r;
        tag_announce_extras(&ann[i], &r);
        if (own < 0 && tag_roster_cmp(&r, &tag_g.net_local) == 0) {
            own = i;
            continue;
        }
        if (tag_g.learned_count < TAG_PORTS - 1) {
            tag_g.learned[tag_g.learned_count++] = r;
        }
    }
}

static void tag_decide(TagMatch* m)
{
    TagAnnounce ann[TAG_PORTS];
    int owner[TAG_PORTS];
    u8 index[TAG_PORTS];
    u16 avail;
    int n = 0, i, k;

    for (i = 0; i < TAG_PORTS; i++) {
        if (Player_GetPlayerSlotType(i) == Gm_PKind_Human) {
            ann[n] = m->pl[i].announce;
            owner[n] = i;
            n++;
        }
    }
    tag_learn(ann, n);
    if (!tag_rules_net_decide(ann, n, tag_config_hash(), index, &avail)) {
        m->phase = TAG_PHASE_OFF;
        for (i = 0; i < TAG_PORTS; i++) {
            m->pl[i].enabled = 0;
        }
        return;
    }
    /*
     * Every client preloaded these rosters in this order, so a plan index
     * is the same roster everywhere.  Equal rosters share their loaded
     * files, so their availability bits agree too.
     */
    for (i = 0; i < n; i++) {
        TagPlayer* p = &m->pl[owner[i]];
        TagRosterExtras r;
        tag_announce_extras(&ann[i], &r);
        for (k = 0; k < 3; k++) {
            TagSlot* s = &p->slot[k + 1];
            s->ckind = r.ckind[k];
            s->costume = r.costume[k];
            if (!tag_ckind_valid(r.ckind[k])) {
                s->state = TAG_SLOT_EMPTY;
            } else if ((avail >> (index[i] * 3 + k)) & 1) {
                s->state = TAG_SLOT_READY;
            } else {
                s->state = TAG_SLOT_UNLOADED;
            }
        }
        p->enabled = !tag_roster_empty(&r);
    }
    m->phase = TAG_PHASE_ACTIVE;
}

/*
 * Every frame of a netplay match: feed each human port's buttons to its
 * decoder, take the side channel off the game's view of the pads, and
 * decide once all announcements are in.
 */
void tag_net_frame(TagMatch* m)
{
    int i, pending = 0;

    for (i = 0; i < TAG_PORTS; i++) {
        TagPlayer* p = &m->pl[i];
        HSD_PadStatus* gs;
        if (Player_GetPlayerSlotType(i) != Gm_PKind_Human) {
            continue;
        }
        if (tag_net_rx_feed(&p->rx, HSD_PadMasterStatus[p->port].button) &&
            !p->announced)
        {
            p->announce = p->rx.last;
            p->announced = 1;
        }
        if (!p->announced) {
            pending = 1;
        }
        gs = &HSD_PadGameStatus[p->port];
        gs->button &= ~TAG_PAD_NET;
        gs->last_button &= ~TAG_PAD_NET;
        gs->trigger &= ~TAG_PAD_NET;
        gs->release &= ~TAG_PAD_NET;
        gs->repeat &= ~TAG_PAD_NET;
    }
    if (m->phase != TAG_PHASE_ROSTERS) {
        return;
    }
    if (!pending) {
        tag_decide(m);
    } else if (m->frame > TAG_NET_DEADLINE) {
        m->phase = TAG_PHASE_OFF;
        for (i = 0; i < TAG_PORTS; i++) {
            m->pl[i].enabled = 0;
        }
    }
}
