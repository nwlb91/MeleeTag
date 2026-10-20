/*
 * Pure rule logic; see tag_rules.h.  Built for the payload and for the host
 * test suite, so it uses no engine headers and no libc beyond nothing.
 */
#include "tag_rules.h"

static const tag_u32 tag_dpad_order[TAG_SLOTS] = {
    TAG_PAD_UP, TAG_PAD_RIGHT, TAG_PAD_DOWN, TAG_PAD_LEFT,
};

int tag_rules_dpad_slot(tag_u32 dpad_bit)
{
    int i;
    for (i = 0; i < TAG_SLOTS; i++) {
        if (dpad_bit == tag_dpad_order[i]) {
            return i;
        }
    }
    return -1;
}

int tag_rules_fresh_slot(tag_u32 prev_held, tag_u32 now_held)
{
    tag_u32 fresh = now_held & ~prev_held & TAG_PAD_DPAD;
    int i;

    for (i = 0; i < TAG_SLOTS; i++) {
        if (fresh & tag_dpad_order[i]) {
            return i;
        }
    }
    return -1;
}

int tag_rules_decide(const TagRequest* req)
{
    if (req->slot < 0 || req->slot >= TAG_SLOTS) {
        return TAG_DECIDE_NONE;
    }
    /* Checked first so it never costs a cooldown or shows as a failure. */
    if (req->slot == req->active) {
        return TAG_DECIDE_SAME;
    }
    if (!(req->ready_mask & (1u << req->slot))) {
        return TAG_DECIDE_EMPTY;
    }
    if (req->cooldown != 0) {
        return TAG_DECIDE_COOLDOWN;
    }
    if (!req->actionable) {
        return TAG_DECIDE_BUSY;
    }
    return TAG_DECIDE_SWITCH;
}

tag_u32 tag_rules_tick(tag_u32 cooldown)
{
    return cooldown != 0 ? cooldown - 1 : 0;
}

int tag_rules_state_class(int id)
{
    if (id < 0 || id >= TAG_MS_COMMON_COUNT) {
        /*
         * Character-specific states.  Their interrupt windows are written
         * per character and the IASA flag is only reset by the Enter
         * functions of states that read it, so a set flag here can be stale.
         * None of them are treated as actionable.
         */
        return TAG_CLASS_NEVER;
    }
    switch (id) {
    case TAG_MS_WAIT:
    case TAG_MS_WALKSLOW:
    case TAG_MS_WALKMIDDLE:
    case TAG_MS_WALKFAST:
    case TAG_MS_TURN:
    case TAG_MS_RUN:
    case TAG_MS_SQUATRV:
    case TAG_MS_OTTOTTO:
    case TAG_MS_OTTOTTOWAIT:
        return TAG_CLASS_GROUND;
    case TAG_MS_SQUAT:
    case TAG_MS_SQUATWAIT:
        return TAG_CLASS_CROUCH;
    case TAG_MS_JUMPF:
    case TAG_MS_JUMPB:
    case TAG_MS_JUMPAERIALF:
    case TAG_MS_JUMPAERIALB:
        return TAG_CLASS_AIR;
    case TAG_MS_DAMAGEFALL:
        return TAG_CLASS_TUMBLE;
    case TAG_MS_LANDING:
    case TAG_MS_LANDINGFALLSPECIAL:
        return TAG_CLASS_LANDING;
    case TAG_MS_ATTACKDASH:
    case TAG_MS_ATTACK100END:
        return TAG_CLASS_GROUND_IASA;
    default:
        break;
    }
    if (id >= TAG_MS_FALL && id <= TAG_MS_FALLAERIALB) {
        return TAG_CLASS_AIR; /* Fall, FallF/B, FallAerial, FallAerialF/B */
    }
    if ((id >= TAG_MS_ATTACK11 && id <= TAG_MS_ATTACK13) ||
        (id >= TAG_MS_ATTACKS3HI && id <= TAG_MS_ATTACKLW4))
    {
        return TAG_CLASS_GROUND_IASA;
    }
    if (id >= TAG_MS_ATTACKAIRN && id <= TAG_MS_ATTACKAIRLW) {
        return TAG_CLASS_AIR_IASA;
    }
    if (id >= TAG_MS_DAMAGEHI1 && id <= TAG_MS_DAMAGEAIR3) {
        return TAG_CLASS_FLINCH;
    }
    if (id >= TAG_MS_DAMAGEFLYHI && id <= TAG_MS_DAMAGEFLYROLL) {
        return TAG_CLASS_FLY;
    }
    /*
     * Everything else stays put: dash, run turn/brake, jump squat, shield,
     * dodges, helpless fall, aerial and helpless landing lag, ledge, grabs,
     * throws, knockdown and techs, dizzy, sleep, bury, item states, entry,
     * respawn and death.
     */
    return TAG_CLASS_NEVER;
}

static int tag_air_entry(const TagFighterView* v)
{
    return v->airborne ? TAG_ENTRY_FALL : TAG_ENTRY_WAIT;
}

int tag_rules_entry(const TagFighterView* v)
{
    if (v->sleeping || v->input_disabled || v->in_hitlag || v->in_hitstun ||
        v->grab_linked || v->holding_hammer)
    {
        return TAG_ENTRY_NONE;
    }
    switch (tag_rules_state_class(v->motion_id)) {
    case TAG_CLASS_GROUND:
        return TAG_ENTRY_WAIT;
    case TAG_CLASS_CROUCH:
        return TAG_ENTRY_SQUAT;
    case TAG_CLASS_AIR:
        return TAG_ENTRY_FALL;
    case TAG_CLASS_TUMBLE:
        return TAG_ENTRY_TUMBLE;
    case TAG_CLASS_FLINCH:
        /* ftCo_Damage_IASA: Wait or Fall interrupts once hitstun ends. */
        return tag_air_entry(v);
    case TAG_CLASS_FLY:
        /* ftCo_DamageFly_IASA defers to DamageFall: keep the tumble. */
        return v->airborne ? TAG_ENTRY_TUMBLE : TAG_ENTRY_WAIT;
    case TAG_CLASS_GROUND_IASA:
        return v->allow_interrupt ? TAG_ENTRY_WAIT : TAG_ENTRY_NONE;
    case TAG_CLASS_AIR_IASA:
        return v->allow_interrupt ? TAG_ENTRY_FALL : TAG_ENTRY_NONE;
    case TAG_CLASS_LANDING:
        /* ftCo_Landing_IASA: both the per-entry flag and the lag frames. */
        if (v->landing_allowed && v->anim_frame >= v->landing_lag) {
            return TAG_ENTRY_WAIT;
        }
        return TAG_ENTRY_NONE;
    default:
        return TAG_ENTRY_NONE;
    }
}

int tag_rules_partner_safe(const TagFighterView* v)
{
    if (v->sleeping) {
        return 1; /* already out of play (dead this stock) */
    }
    if (v->in_hitlag || v->in_hitstun || v->grab_linked) {
        return 0;
    }
    /*
     * Free states only.  An attack in progress is simply cut short with the
     * leader; tumble, knockdown, helpless fall, ledge, grabs and the
     * partner's own specials all wait until the partner is free again.
     */
    switch (tag_rules_state_class(v->motion_id)) {
    case TAG_CLASS_GROUND:
    case TAG_CLASS_CROUCH:
    case TAG_CLASS_AIR:
    case TAG_CLASS_FLINCH:
    case TAG_CLASS_GROUND_IASA:
    case TAG_CLASS_AIR_IASA:
    case TAG_CLASS_LANDING:
        return 1;
    default:
        return 0;
    }
}

tag_u32 tag_rules_map_jumps(int airborne, tag_u32 used_out, tag_u32 max_out,
                            tag_u32 max_in)
{
    tag_u32 used, left_out, left_in;

    if (!airborne) {
        return 0;
    }
    if (max_in <= 1) {
        return max_in;
    }
    if (max_out <= 1) {
        return max_in;
    }
    used = used_out < 1 ? 1 : used_out; /* airborne always spent the first */
    left_out = used >= max_out ? 0 : max_out - used;
    left_in = left_out * (max_in - 1) / (max_out - 1);
    if (left_in > max_in - 1) {
        left_in = max_in - 1;
    }
    return max_in - left_in;
}

/* ---- Codec -------------------------------------------------------------- */

tag_u8 tag_crc8(const tag_u8* p, tag_u32 n)
{
    tag_u8 crc = 0;
    tag_u32 i;
    int b;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (tag_u8) ((crc << 1) ^ 0x07) : (tag_u8) (crc << 1);
        }
    }
    return crc;
}

tag_u32 tag_hash32(tag_u32 h, tag_u32 v)
{
    int i;
    for (i = 0; i < 4; i++) {
        h ^= (v >> (i * 8)) & 0xFF;
        h *= 16777619u; /* FNV-1a */
    }
    return h;
}

typedef struct TagBitWriter {
    tag_u8* p;
    tag_u32 pos;
} TagBitWriter;

static void tag_put(TagBitWriter* w, tag_u32 value, int bits)
{
    int i;
    for (i = bits - 1; i >= 0; i--) {
        if ((value >> i) & 1) {
            w->p[w->pos >> 3] |= (tag_u8) (0x80 >> (w->pos & 7));
        }
        w->pos++;
    }
}

static tag_u32 tag_get(const tag_u8* p, tag_u32* pos, int bits)
{
    tag_u32 v = 0;
    int i;
    for (i = 0; i < bits; i++) {
        v = (v << 1) | ((p[*pos >> 3] >> (7 - (*pos & 7))) & 1);
        (*pos)++;
    }
    return v;
}

void tag_net_encode(const TagAnnounce* a, tag_u8 out[TAG_NET_BYTES])
{
    TagBitWriter w;
    int i;

    for (i = 0; i < TAG_NET_BYTES; i++) {
        out[i] = 0;
    }
    w.p = out;
    w.pos = 0;
    tag_put(&w, a->version, 2);
    tag_put(&w, a->config, 8);
    for (i = 0; i < 3; i++) {
        tag_put(&w, a->ckind[i] == TAG_CKIND_NONE ? TAG_NET_CKIND_NONE
                                                  : a->ckind[i],
                5);
    }
    for (i = 0; i < 3; i++) {
        tag_put(&w, a->ckind[i] == TAG_CKIND_NONE ? 0 : a->costume[i], 3);
    }
    tag_put(&w, a->preload, 16);
    tag_put(&w, a->avail, 12);
    tag_put(&w, 0, 10); /* reserved; 72 bits so far */
    out[TAG_NET_BYTES - 1] = tag_crc8(out, TAG_NET_BYTES - 1);
}

int tag_net_decode(const tag_u8 in[TAG_NET_BYTES], TagAnnounce* a)
{
    tag_u32 pos = 0;
    int i;

    if (tag_crc8(in, TAG_NET_BYTES - 1) != in[TAG_NET_BYTES - 1]) {
        return 0;
    }
    a->version = (tag_u8) tag_get(in, &pos, 2);
    a->config = (tag_u8) tag_get(in, &pos, 8);
    for (i = 0; i < 3; i++) {
        tag_u32 ck = tag_get(in, &pos, 5);
        if (ck == TAG_NET_CKIND_NONE) {
            a->ckind[i] = TAG_CKIND_NONE;
        } else if (ck < TAG_CKIND_PLAYABLE) {
            a->ckind[i] = (tag_u8) ck;
        } else {
            return 0;
        }
    }
    for (i = 0; i < 3; i++) {
        a->costume[i] = (tag_u8) tag_get(in, &pos, 3);
    }
    a->preload = (tag_u16) tag_get(in, &pos, 16);
    a->avail = (tag_u16) tag_get(in, &pos, 12);
    if (tag_get(in, &pos, 10) != 0 || a->version != TAG_NET_VERSION) {
        return 0;
    }
    return 1;
}

tag_u32 tag_net_tx_bits(const tag_u8 msg[TAG_NET_BYTES], tag_u32 phase)
{
    tag_u32 bits = 0;

    phase %= TAG_NET_BITS;
    if (phase == 0) {
        bits |= TAG_PAD_NET_MARK;
    }
    if ((msg[phase >> 3] >> (7 - (phase & 7))) & 1) {
        bits |= TAG_PAD_NET_DATA;
    }
    return bits;
}

void tag_net_rx_reset(TagNetRx* rx)
{
    int i;
    for (i = 0; i < TAG_NET_BYTES; i++) {
        rx->buf[i] = 0;
    }
    rx->count = 0xFF;
    rx->valid = 0;
}

int tag_net_rx_feed(TagNetRx* rx, tag_u32 buttons)
{
    int i;

    if (buttons & TAG_PAD_NET_MARK) {
        for (i = 0; i < TAG_NET_BYTES; i++) {
            rx->buf[i] = 0;
        }
        rx->count = 0;
    } else if (rx->count == 0xFF) {
        return 0;
    }
    if (buttons & TAG_PAD_NET_DATA) {
        rx->buf[rx->count >> 3] |= (tag_u8) (0x80 >> (rx->count & 7));
    }
    rx->count++;
    if (rx->count < TAG_NET_BITS) {
        return 0;
    }
    rx->count = 0xFF;
    if (!tag_net_decode(rx->buf, &rx->last)) {
        return 0;
    }
    rx->valid = 1;
    return 1;
}

/* ---- Rosters ------------------------------------------------------------ */

tag_u16 tag_roster_hash(const TagRosterExtras* r)
{
    tag_u32 h = 2166136261u;
    int i;

    for (i = 0; i < 3; i++) {
        h = tag_hash32(h, ((tag_u32) r->ckind[i] << 8) |
                              (r->ckind[i] == TAG_CKIND_NONE ? 0 : r->costume[i]));
    }
    return (tag_u16) (h ^ (h >> 16));
}

int tag_roster_empty(const TagRosterExtras* r)
{
    return r->ckind[0] == TAG_CKIND_NONE && r->ckind[1] == TAG_CKIND_NONE &&
           r->ckind[2] == TAG_CKIND_NONE;
}

int tag_roster_cmp(const TagRosterExtras* a, const TagRosterExtras* b)
{
    tag_u16 ha = tag_roster_hash(a), hb = tag_roster_hash(b);
    int i;

    if (ha != hb) {
        return ha < hb ? -1 : 1;
    }
    for (i = 0; i < 3; i++) {
        if (a->ckind[i] != b->ckind[i]) {
            return a->ckind[i] < b->ckind[i] ? -1 : 1;
        }
        if (a->costume[i] != b->costume[i]) {
            return a->costume[i] < b->costume[i] ? -1 : 1;
        }
    }
    return 0;
}

void tag_rules_sort_rosters(TagRosterExtras* r, tag_u8* perm, int n)
{
    int i, j;

    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && tag_roster_cmp(&r[j], &r[j - 1]) < 0; j--) {
            TagRosterExtras t = r[j];
            r[j] = r[j - 1];
            r[j - 1] = t;
            if (perm != 0) {
                tag_u8 p = perm[j];
                perm[j] = perm[j - 1];
                perm[j - 1] = p;
            }
        }
    }
}

tag_u16 tag_rules_rosters_hash(const TagRosterExtras* sorted, int n)
{
    tag_u32 h = 2166136261u;
    int i;

    for (i = 0; i < n; i++) {
        h = tag_hash32(h, tag_roster_hash(&sorted[i]));
    }
    return (tag_u16) (h ^ (h >> 16));
}

void tag_announce_extras(const TagAnnounce* a, TagRosterExtras* out)
{
    int k;
    for (k = 0; k < 3; k++) {
        out->ckind[k] = a->ckind[k];
        out->costume[k] = a->ckind[k] == TAG_CKIND_NONE ? 0 : a->costume[k];
    }
}

int tag_rules_net_decide(const TagAnnounce* ann, int n, tag_u8 config,
                         tag_u8* plan_index, tag_u16* avail)
{
    TagRosterExtras r[8];
    tag_u8 perm[8];
    tag_u16 h;
    int i, ok = 1;

    if (n < 1 || n > 8) {
        return 0;
    }
    *avail = 0x0FFF;
    for (i = 0; i < n; i++) {
        tag_announce_extras(&ann[i], &r[i]);
        perm[i] = (tag_u8) i;
        *avail &= ann[i].avail;
        if (ann[i].config != config) {
            ok = 0;
        }
    }
    tag_rules_sort_rosters(r, perm, n);
    h = tag_rules_rosters_hash(r, n);
    for (i = 0; i < n; i++) {
        plan_index[perm[i]] = (tag_u8) i;
        if (ann[i].preload != h) {
            ok = 0;
        }
    }
    return ok;
}

int tag_rules_admission_order(int rosters, tag_u8* order)
{
    int n = 0;
    int slot, r;

    for (slot = 0; slot < 3; slot++) {
        for (r = 0; r < rosters; r++) {
            order[n++] = (tag_u8) (r * 3 + slot);
        }
    }
    return n;
}

tag_u32 tag_rules_next_costume(tag_u32 cur, tag_u32 count)
{
    if (count == 0) {
        return 0;
    }
    return (cur + 1) % count;
}
