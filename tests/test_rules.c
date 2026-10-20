/*
 * Host tests for the pure rule logic (mod/src/tag_rules.c): the same source
 * file the payload is built from.
 */
#include <stdio.h>
#include <string.h>

#include "tag_rules.h"

static int failures;
static int checks;

#define CHECK(cond)                                                           \
    do {                                                                      \
        checks++;                                                             \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                     \
    } while (0)

static TagFighterView view(int motion, int airborne)
{
    TagFighterView v;
    memset(&v, 0, sizeof(v));
    v.motion_id = motion;
    v.airborne = airborne;
    v.landing_lag = 4.0F;
    return v;
}

static void test_dpad(void)
{
    CHECK(tag_rules_dpad_slot(TAG_PAD_UP) == 0);
    CHECK(tag_rules_dpad_slot(TAG_PAD_RIGHT) == 1);
    CHECK(tag_rules_dpad_slot(TAG_PAD_DOWN) == 2);
    CHECK(tag_rules_dpad_slot(TAG_PAD_LEFT) == 3);

    /* Fresh press only. */
    CHECK(tag_rules_fresh_slot(0, TAG_PAD_RIGHT) == 1);
    CHECK(tag_rules_fresh_slot(TAG_PAD_RIGHT, TAG_PAD_RIGHT) == -1);
    CHECK(tag_rules_fresh_slot(TAG_PAD_RIGHT, 0) == -1);
    /* Holding one direction and pressing another: the new one only. */
    CHECK(tag_rules_fresh_slot(TAG_PAD_RIGHT, TAG_PAD_RIGHT | TAG_PAD_LEFT) == 3);
    /* Two at once: fixed order Up, Right, Down, Left. */
    CHECK(tag_rules_fresh_slot(0, TAG_PAD_LEFT | TAG_PAD_DOWN) == 2);
    CHECK(tag_rules_fresh_slot(0, TAG_PAD_UP | TAG_PAD_LEFT) == 0);
    /* Non-D-pad bits are ignored. */
    CHECK(tag_rules_fresh_slot(0, 0x0100 | TAG_PAD_NET) == -1);
}

static void test_holding_never_repeats(void)
{
    tag_u32 prev = 0;
    int presses = 0, frame;

    for (frame = 0; frame < 600; frame++) {
        tag_u32 held = TAG_PAD_DOWN; /* held for ten seconds */
        if (tag_rules_fresh_slot(prev, held) >= 0) {
            presses++;
        }
        prev = held;
    }
    CHECK(presses == 1);
}

static void test_decide(void)
{
    TagRequest r;

    r.slot = 1;
    r.active = 0;
    r.ready_mask = 0xF;
    r.cooldown = 0;
    r.actionable = 1;
    CHECK(tag_rules_decide(&r) == TAG_DECIDE_SWITCH);

    r.slot = -1;
    CHECK(tag_rules_decide(&r) == TAG_DECIDE_NONE);

    /* Current slot: nothing, even during cooldown or when busy. */
    r.slot = 0;
    r.cooldown = 10;
    r.actionable = 0;
    CHECK(tag_rules_decide(&r) == TAG_DECIDE_SAME);

    r.slot = 2;
    r.ready_mask = 0x3;
    CHECK(tag_rules_decide(&r) == TAG_DECIDE_EMPTY);

    r.ready_mask = 0xF;
    r.cooldown = 1;
    r.actionable = 1;
    CHECK(tag_rules_decide(&r) == TAG_DECIDE_COOLDOWN);

    r.cooldown = 0;
    r.actionable = 0;
    CHECK(tag_rules_decide(&r) == TAG_DECIDE_BUSY);
}

static void test_cooldown(void)
{
    tag_u32 c = 30;
    int frames = 0;

    while (c != 0) {
        c = tag_rules_tick(c);
        frames++;
    }
    CHECK(frames == 30);
    CHECK(tag_rules_tick(0) == 0);
}

/*
 * A request rejected for cooldown is not queued: after the cooldown ends,
 * nothing happens until a new press arrives.
 */
static void test_no_queue(void)
{
    tag_u32 prev = 0, cooldown = 30;
    int switches = 0, frame;

    for (frame = 0; frame < 120; frame++) {
        tag_u32 held = frame == 5 ? TAG_PAD_RIGHT : 0; /* one tap, mid-cooldown */
        TagRequest r;
        r.slot = tag_rules_fresh_slot(prev, held);
        r.active = 0;
        r.ready_mask = 0xF;
        r.cooldown = cooldown;
        r.actionable = 1;
        if (tag_rules_decide(&r) == TAG_DECIDE_SWITCH) {
            switches++;
        }
        cooldown = tag_rules_tick(cooldown);
        prev = held;
    }
    CHECK(switches == 0);
}

static void test_states(void)
{
    TagFighterView v;

    v = view(TAG_MS_WAIT, 0);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_WAIT);
    v = view(TAG_MS_RUN, 0);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_WAIT);
    v = view(TAG_MS_SQUATWAIT, 0);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_SQUAT);
    v = view(TAG_MS_FALL, 1);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_FALL);
    v = view(TAG_MS_JUMPAERIALF, 1);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_FALL);
    v = view(TAG_MS_DAMAGEFALL, 1);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_TUMBLE);

    /* Not actionable no matter what. */
    v = view(TAG_MS_WAIT, 0);
    v.in_hitlag = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v = view(TAG_MS_WAIT, 0);
    v.input_disabled = 1; /* countdown, GAME! */
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v = view(TAG_MS_WAIT, 0);
    v.grab_linked = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v = view(TAG_MS_WAIT, 0);
    v.holding_hammer = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v = view(TAG_MS_WAIT, 0);
    v.sleeping = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);

    /* Hitstun: flinch and launch states wait for it to end. */
    v = view(TAG_MS_DAMAGEFLYHI, 1);
    v.in_hitstun = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v.in_hitstun = 0;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_TUMBLE); /* tumble carries over */
    v = view(TAG_MS_DAMAGEHI1, 0);
    v.in_hitstun = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v.in_hitstun = 0;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_WAIT);
    v = view(TAG_MS_DAMAGEAIR1, 1);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_FALL);

    /* Attacks: only inside the IASA window. */
    v = view(TAG_MS_ATTACKS3HI, 0);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v.allow_interrupt = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_WAIT);
    v = view(TAG_MS_ATTACKAIRN, 1);
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v.allow_interrupt = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_FALL);

    /* Landing lag: the engine's own window (flag and frame). */
    v = view(TAG_MS_LANDING, 0);
    v.landing_allowed = 1;
    v.anim_frame = 2.0F;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v.anim_frame = 4.0F;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_WAIT);
    v = view(TAG_MS_LANDINGFALLSPECIAL, 0); /* helpless landing */
    v.anim_frame = 30.0F;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);

    /* A stale IASA flag on a state that does not use it means nothing. */
    v = view(TAG_MS_FALLAERIALB + 1, 1); /* FallSpecial: helpless */
    v.allow_interrupt = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
    v = view(TAG_MS_COMMON_COUNT + 5, 0); /* character-specific state */
    v.allow_interrupt = 1;
    CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);

    /* Shield, ledge, knockdown, respawn, death. */
    {
        static const int never[] = {
            0,   /* DeadDown */
            TAG_MS_SLEEP,
            TAG_MS_REBIRTH,
            TAG_MS_REBIRTHWAIT,
            20,  /* Dash */
            24,  /* KneeBend */
            35,  /* FallSpecial */
            48,  /* Attack100Loop */
            70,  /* LandingAirN */
            178, /* GuardOn */
            179, /* Guard */
            183, /* DownBoundU */
            184, /* DownWaitU */
            199, /* Passive */
            205, /* ShieldBreakFly */
            211, /* Furafura */
            233, /* EscapeF */
            239, /* ThrownF */
            244, /* Pass */
            92,  /* LightGet */
            307, /* HammerWait */
            19,  /* TurnRun */
            23,  /* RunBrake */
            212, /* Catch */
            223, /* CapturePulledHi */
            236, /* EscapeAir */
            252, /* CliffCatch */
            253, /* CliffWait */
            264, /* AppealSR */
            322, /* Entry */
        };
        unsigned i;
        for (i = 0; i < sizeof(never) / sizeof(never[0]); i++) {
            v = view(never[i], 0);
            v.allow_interrupt = 1;
            v.landing_allowed = 1;
            v.anim_frame = 99.0F;
            CHECK(tag_rules_entry(&v) == TAG_ENTRY_NONE);
        }
    }
}

static void test_partner(void)
{
    TagFighterView v;

    v = view(TAG_MS_WAIT, 0);
    CHECK(tag_rules_partner_safe(&v));
    v = view(TAG_MS_WAIT, 0);
    v.sleeping = 1; /* dead this stock */
    v.in_hitstun = 1;
    CHECK(tag_rules_partner_safe(&v));
    v = view(TAG_MS_DAMAGEFLYHI + 1, 1); /* DamageFlyN */
    CHECK(!tag_rules_partner_safe(&v));
    v = view(TAG_MS_DAMAGEFALL, 1);
    CHECK(!tag_rules_partner_safe(&v));
    v = view(TAG_MS_WAIT, 0);
    v.in_hitlag = 1;
    CHECK(!tag_rules_partner_safe(&v));
    v = view(TAG_MS_WAIT, 0);
    v.grab_linked = 1;
    CHECK(!tag_rules_partner_safe(&v));
    v = view(35, 1); /* helpless */
    CHECK(!tag_rules_partner_safe(&v));
    v = view(184, 0); /* knocked down */
    CHECK(!tag_rules_partner_safe(&v));
    v = view(TAG_MS_ATTACKS3HI, 0); /* mid attack: just cut short */
    CHECK(tag_rules_partner_safe(&v));
}

static void test_jumps(void)
{
    tag_u32 out, max_out, max_in, chain;

    /* Grounded: nothing used. */
    CHECK(tag_rules_map_jumps(0, 1, 2, 6) == 0);
    /* Fox with his double jump left -> Kirby with all midair jumps. */
    CHECK(tag_rules_map_jumps(1, 1, 2, 6) == 1);
    /* Fox without it -> Kirby without any. */
    CHECK(tag_rules_map_jumps(1, 2, 2, 6) == 6);
    /* Kirby with one of five left -> Fox: floor(1/5) = none. */
    CHECK(tag_rules_map_jumps(1, 5, 6, 2) == 2);
    /* Kirby with all five -> Fox with his one. */
    CHECK(tag_rules_map_jumps(1, 1, 6, 2) == 1);
    /* Airborne with 0 recorded counts the first jump as spent. */
    CHECK(tag_rules_map_jumps(1, 0, 2, 2) == 1);
    /* Same character: unchanged. */
    for (out = 1; out <= 6; out++) {
        CHECK(tag_rules_map_jumps(1, out, 6, 6) == out);
    }

    /*
     * No chain of switches creates jumps: the remaining fraction never
     * grows.  Try every chain of up to four switches between max-jump
     * values 2..6 starting from every state.
     */
    for (max_out = 2; max_out <= 6; max_out++) {
        for (out = 1; out <= max_out; out++) {
            for (chain = 0; chain < 625; chain++) {
                tag_u32 c = chain, cur_max = max_out, used = out;
                double frac0 = (double) (max_out - out) / (max_out - 1);
                int step;
                for (step = 0; step < 4; step++) {
                    max_in = 2 + c % 5;
                    c /= 5;
                    used = tag_rules_map_jumps(1, used, cur_max, max_in);
                    cur_max = max_in;
                    CHECK((double) (cur_max - used) / (cur_max - 1) <=
                          frac0 + 1e-9);
                }
            }
        }
    }
}

static void test_codec(void)
{
    TagAnnounce a, b;
    tag_u8 msg[TAG_NET_BYTES];
    TagNetRx rx;
    tag_u32 phase, frame;
    int got = 0;

    memset(&a, 0, sizeof(a));
    a.version = TAG_NET_VERSION;
    a.config = 0xA5;
    a.ckind[0] = 0x02; /* Fox */
    a.ckind[1] = TAG_CKIND_NONE;
    a.ckind[2] = 0x13; /* Sheik */
    a.costume[0] = 3;
    a.costume[2] = 1;
    a.preload = 0xBEEF;
    a.avail = 0x0A5;
    tag_net_encode(&a, msg);
    CHECK(tag_net_decode(msg, &b));
    CHECK(b.config == a.config && b.preload == a.preload && b.avail == a.avail);
    CHECK(b.ckind[0] == 0x02 && b.ckind[1] == TAG_CKIND_NONE && b.ckind[2] == 0x13);
    CHECK(b.costume[0] == 3 && b.costume[2] == 1);

    /* Any single flipped bit is caught. */
    for (phase = 0; phase < TAG_NET_BITS; phase++) {
        tag_u8 bad[TAG_NET_BYTES];
        memcpy(bad, msg, sizeof(bad));
        bad[phase / 8] ^= (tag_u8) (0x80 >> (phase % 8));
        CHECK(!tag_net_decode(bad, &b));
    }

    /*
     * Over the pad: the receiver joins mid-stream, with inputs zeroed for
     * the first 78 frames (Slippi's start sync), and still decodes.
     */
    tag_net_rx_reset(&rx);
    for (frame = 0; frame < 400; frame++) {
        tag_u32 buttons = 0x0100; /* A held, irrelevant */
        if (frame >= 78) {
            buttons |= tag_net_tx_bits(msg, frame + 17);
        }
        if (tag_net_rx_feed(&rx, buttons)) {
            got++;
            CHECK(rx.last.preload == 0xBEEF);
        }
    }
    CHECK(got >= 3);
}

static void test_rosters(void)
{
    TagRosterExtras r1, r2;
    tag_u8 order[12];
    int n, i;

    memset(&r1, 0, sizeof(r1));
    r1.ckind[0] = 2;
    r1.ckind[1] = TAG_CKIND_NONE;
    r1.ckind[2] = TAG_CKIND_NONE;
    r2 = r1;
    CHECK(tag_roster_hash(&r1) == tag_roster_hash(&r2));
    CHECK(tag_roster_cmp(&r1, &r2) == 0);
    r2.costume[0] = 1;
    CHECK(tag_roster_cmp(&r1, &r2) != 0);
    CHECK(tag_roster_cmp(&r1, &r2) == -tag_roster_cmp(&r2, &r1));
    /* The costume of an empty slot does not matter. */
    r2 = r1;
    r2.costume[1] = 5;
    CHECK(tag_roster_hash(&r1) == tag_roster_hash(&r2));

    n = tag_rules_admission_order(2, order);
    CHECK(n == 6);
    /* Slot 1 of both rosters, then slot 2 of both, then slot 3. */
    {
        static const tag_u8 want[6] = { 0, 3, 1, 4, 2, 5 };
        for (i = 0; i < 6; i++) {
            CHECK(order[i] == want[i]);
        }
    }
    CHECK(tag_rules_next_costume(0, 4) == 1);
    CHECK(tag_rules_next_costume(3, 4) == 0);
}

static TagRosterExtras roster(int a, int b, int c)
{
    TagRosterExtras r;
    r.ckind[0] = (tag_u8) a;
    r.ckind[1] = (tag_u8) b;
    r.ckind[2] = (tag_u8) c;
    r.costume[0] = r.costume[1] = r.costume[2] = 0;
    return r;
}

/* What a client announces given its own roster and what it preloaded. */
static TagAnnounce announce(TagRosterExtras own, const TagRosterExtras* pre,
                            int n, tag_u16 avail, tag_u8 config)
{
    TagRosterExtras sorted[4];
    TagAnnounce a;
    int i;

    for (i = 0; i < n; i++) {
        sorted[i] = pre[i];
    }
    tag_rules_sort_rosters(sorted, 0, n);
    memset(&a, 0, sizeof(a));
    a.version = TAG_NET_VERSION;
    a.config = config;
    for (i = 0; i < 3; i++) {
        a.ckind[i] = own.ckind[i];
        a.costume[i] = own.costume[i];
    }
    a.preload = tag_rules_rosters_hash(sorted, n);
    a.avail = avail;
    return a;
}

static void test_net_verdict(void)
{
    TagRosterExtras ra = roster(2, 0x13, TAG_CKIND_NONE);  /* Fox, Sheik */
    TagRosterExtras rb = roster(0x14, 0x0E, 4);             /* Falco, ICs, Kirby */
    TagRosterExtras none = roster(TAG_CKIND_NONE, TAG_CKIND_NONE, TAG_CKIND_NONE);
    TagAnnounce ann[2];
    tag_u8 idx_a[2], idx_b[2];
    tag_u16 av_a, av_b;
    int ok_a, ok_b;

    /*
     * First match against a new opponent: each client only knows its own
     * roster (the opponent's learned roster is empty), so the preload
     * hashes cannot match and both clients turn tag off.
     */
    {
        TagRosterExtras pre_a[2], pre_b[2];
        pre_a[0] = ra;
        pre_a[1] = none;
        pre_b[0] = rb;
        pre_b[1] = none;
        ann[0] = announce(ra, pre_a, 2, 0x0FFF, 7);
        ann[1] = announce(rb, pre_b, 2, 0x0FFF, 7);
        ok_a = tag_rules_net_decide(ann, 2, 7, idx_a, &av_a);
        ok_b = tag_rules_net_decide(ann, 2, 7, idx_b, &av_b);
        CHECK(!ok_a && !ok_b);
    }
    /*
     * Second match: both learned the other's roster from the announcements
     * and preloaded it; port order differs between the two clients' views
     * of who is "own", but the verdict is computed from the same port-
     * ordered announcements on both clients.
     */
    {
        TagRosterExtras pre_a[2], pre_b[2];
        pre_a[0] = ra;
        pre_a[1] = rb;
        pre_b[0] = rb;
        pre_b[1] = ra;
        /* Client A could load everything; client B is missing A's slot 2. */
        ann[0] = announce(ra, pre_a, 2, 0x0FFF, 7);
        ann[1] = announce(rb, pre_b, 2, 0x0FFF & ~0, 7);
        {
            /* B's view: plan index of ra in the sorted order. */
            TagRosterExtras s[2];
            tag_u8 perm[2] = { 0, 1 };
            int ra_index;
            s[0] = ra;
            s[1] = rb;
            tag_rules_sort_rosters(s, perm, 2);
            ra_index = perm[0] == 0 ? 0 : 1;
            ann[1].avail = (tag_u16) (0x0FFF & ~(1u << (ra_index * 3 + 1)));
        }
        ok_a = tag_rules_net_decide(ann, 2, 7, idx_a, &av_a);
        ok_b = tag_rules_net_decide(ann, 2, 7, idx_b, &av_b);
        CHECK(ok_a && ok_b);
        CHECK(av_a == av_b);
        CHECK(idx_a[0] == idx_b[0] && idx_a[1] == idx_b[1]);
        CHECK(idx_a[0] != idx_a[1]);
        /* Player A's slot 2 (Sheik) is off for both; everything else on. */
        CHECK(!((av_a >> (idx_a[0] * 3 + 1)) & 1));
        CHECK((av_a >> (idx_a[0] * 3 + 0)) & 1);
        CHECK((av_a >> (idx_a[1] * 3 + 2)) & 1);
    }
    /* Different build or settings on one client: off for everyone. */
    {
        TagRosterExtras pre[2];
        pre[0] = ra;
        pre[1] = rb;
        ann[0] = announce(ra, pre, 2, 0x0FFF, 7);
        ann[1] = announce(rb, pre, 2, 0x0FFF, 8);
        CHECK(!tag_rules_net_decide(ann, 2, 7, idx_a, &av_a));
        CHECK(!tag_rules_net_decide(ann, 2, 8, idx_b, &av_b));
    }
    /* A roster changed since last match: off, and learned for next time. */
    {
        TagRosterExtras pre_a[2];
        TagRosterExtras rb2 = roster(0x14, 0x0E, 5);
        pre_a[0] = ra;
        pre_a[1] = rb; /* stale */
        ann[0] = announce(ra, pre_a, 2, 0x0FFF, 7);
        {
            TagRosterExtras pre_b[2];
            pre_b[0] = rb2;
            pre_b[1] = ra;
            ann[1] = announce(rb2, pre_b, 2, 0x0FFF, 7);
        }
        CHECK(!tag_rules_net_decide(ann, 2, 7, idx_a, &av_a));
    }
}

int main(void)
{
    test_dpad();
    test_holding_never_repeats();
    test_decide();
    test_cooldown();
    test_no_queue();
    test_states();
    test_partner();
    test_jumps();
    test_codec();
    test_rosters();
    test_net_verdict();
    printf("test_rules: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
