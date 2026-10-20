#ifndef MELEETAG_RULES_H
#define MELEETAG_RULES_H

/*
 * Game rules that do not touch engine memory: input edge detection, the
 * switch decision, the actionability table, the shared-airtime mapping, the
 * netplay announcement codec and roster hashing.  The engine-side code
 * extracts plain values from the fighter and calls into here, so the same
 * code runs in the payload and in the host test suite (tests/test_rules.c).
 *
 * Motion-state ids are the common ids of the NTSC 1.02 engine
 * (ftCo_MS_* in the decomp); src/tag_switch.c static-asserts every constant
 * below against the decomp enum.
 */

typedef unsigned char tag_u8;
typedef unsigned short tag_u16;
typedef unsigned int tag_u32;

#define TAG_SLOTS 4
#define TAG_SLOT_PRIMARY 0
#define TAG_NO_SLOT 0xFF

/* CharacterKind values (CSS ids). */
#define TAG_CKIND_PLAYABLE 0x1A /* CKind_Playable_Count */
#define TAG_CKIND_NONE 0x21     /* ChKind_None */
#define TAG_CKIND_KIRBY 0x04
#define TAG_CKIND_POPONANA 0x0E
#define TAG_CKIND_ZELDA 0x12
#define TAG_CKIND_SEAK 0x13

/* PAD_BUTTON_* bits. */
#define TAG_PAD_LEFT 0x0001u
#define TAG_PAD_RIGHT 0x0002u
#define TAG_PAD_DOWN 0x0004u
#define TAG_PAD_UP 0x0008u
#define TAG_PAD_DPAD 0x000Fu
/* PADStatus.button bits no controller reports; used as the netplay channel. */
#define TAG_PAD_NET_DATA 0x4000u
#define TAG_PAD_NET_MARK 0x8000u
#define TAG_PAD_NET 0xC000u

/* Slot index for each D-pad direction: Up, Right, Down, Left. */
int tag_rules_dpad_slot(tag_u32 dpad_bit);

/*
 * Fresh presses this frame.  Holding a direction never repeats: a bit is
 * fresh only on the frame it goes from released to held.  When several
 * directions become fresh on the same frame the first in Up, Right, Down,
 * Left order wins; the others are dropped, not queued.
 */
int tag_rules_fresh_slot(tag_u32 prev_held, tag_u32 now_held);

enum TagDecision {
    TAG_DECIDE_NONE,     /* no request this frame */
    TAG_DECIDE_SAME,     /* requested the active slot: nothing, no cooldown */
    TAG_DECIDE_EMPTY,    /* slot unassigned or not loaded this match */
    TAG_DECIDE_COOLDOWN, /* shared cooldown still running */
    TAG_DECIDE_BUSY,     /* fighter not in an actionable state */
    TAG_DECIDE_SWITCH,
};

typedef struct TagRequest {
    int slot;              /* requested slot or -1 */
    int active;            /* active slot */
    tag_u32 ready_mask;    /* bit n set: slot n can be switched to */
    tag_u32 cooldown;      /* frames remaining */
    int actionable;        /* engine-side actionability verdict */
} TagRequest;

/* Rejected requests are dropped; nothing is remembered for later frames. */
int tag_rules_decide(const TagRequest* req);

/* One gameplay frame of cooldown. */
tag_u32 tag_rules_tick(tag_u32 cooldown);

/* ---- Actionability ---------------------------------------------------- */

#define TAG_MS_SLEEP 11
#define TAG_MS_REBIRTH 12
#define TAG_MS_REBIRTHWAIT 13
#define TAG_MS_WAIT 14
#define TAG_MS_WALKSLOW 15
#define TAG_MS_WALKMIDDLE 16
#define TAG_MS_WALKFAST 17
#define TAG_MS_TURN 18
#define TAG_MS_RUN 21
#define TAG_MS_JUMPF 25
#define TAG_MS_JUMPB 26
#define TAG_MS_JUMPAERIALF 27
#define TAG_MS_JUMPAERIALB 28
#define TAG_MS_FALL 29
#define TAG_MS_FALLAERIALB 34
#define TAG_MS_DAMAGEFALL 38
#define TAG_MS_SQUAT 39
#define TAG_MS_SQUATWAIT 40
#define TAG_MS_SQUATRV 41
#define TAG_MS_LANDING 42
#define TAG_MS_LANDINGFALLSPECIAL 43
#define TAG_MS_ATTACK11 44
#define TAG_MS_ATTACK13 46
#define TAG_MS_ATTACK100END 49
#define TAG_MS_ATTACKDASH 50
#define TAG_MS_ATTACKS3HI 51
#define TAG_MS_ATTACKLW4 64
#define TAG_MS_ATTACKAIRN 65
#define TAG_MS_ATTACKAIRLW 69
#define TAG_MS_DAMAGEHI1 75
#define TAG_MS_DAMAGELW3 83
#define TAG_MS_DAMAGEAIR1 84
#define TAG_MS_DAMAGEAIR3 86
#define TAG_MS_DAMAGEFLYHI 87
#define TAG_MS_DAMAGEFLYROLL 91
#define TAG_MS_OTTOTTO 245
#define TAG_MS_OTTOTTOWAIT 246
#define TAG_MS_COMMON_COUNT 341

/* How a common state may be left for a switch. */
enum TagStateClass {
    TAG_CLASS_NEVER,
    TAG_CLASS_GROUND,      /* always interruptible, grounded */
    TAG_CLASS_CROUCH,      /* always interruptible, crouching */
    TAG_CLASS_AIR,         /* always interruptible, airborne */
    TAG_CLASS_TUMBLE,      /* DamageFall: airborne, tumble must carry over */
    TAG_CLASS_FLINCH,      /* Damage*: interruptible once hitstun has ended */
    TAG_CLASS_FLY,         /* DamageFly*: as FLINCH, carries tumble over */
    TAG_CLASS_GROUND_IASA, /* attacks: interruptible once the IASA flag is set */
    TAG_CLASS_AIR_IASA,    /* aerials: interruptible once the IASA flag is set */
    TAG_CLASS_LANDING,     /* Landing/LandingFallSpecial: engine's own window */
};

int tag_rules_state_class(int motion_id);

/* The state the incoming fighter is put into. */
enum TagEntry {
    TAG_ENTRY_NONE,   /* not switchable */
    TAG_ENTRY_WAIT,   /* ft_8008A2BC */
    TAG_ENTRY_SQUAT,  /* ftCo_800D638C (SquatWait) */
    TAG_ENTRY_FALL,   /* ftCo_Fall_Enter */
    TAG_ENTRY_TUMBLE, /* ftCo_80090780 (DamageFall) */
};

/* Plain values copied out of a Fighter. */
typedef struct TagFighterView {
    int motion_id;
    int airborne;
    int in_hitlag;        /* x2219_b5 */
    int in_hitstun;       /* x221C_b6 */
    int allow_interrupt;  /* the animation-script IASA flag */
    int landing_allowed;  /* mv.co.landing.allow_interrupt */
    float anim_frame;
    float landing_lag;    /* co_attrs.normal_landing_lag */
    int sleeping;
    int input_disabled;   /* countdown, GAME!, pause transitions */
    int grab_linked;      /* grabbing or grabbed */
    int holding_hammer;   /* hammer forces its own states */
} TagFighterView;

/* TAG_ENTRY_NONE unless the outgoing fighter may be switched out now. */
int tag_rules_entry(const TagFighterView* v);

/*
 * Ice Climbers: the partner is put away with the leader, so it must not be
 * in a state where disappearing would help it (hit, launched, knocked down,
 * grabbed) or would leave someone stranded (grabbing).
 */
int tag_rules_partner_safe(const TagFighterView* v);

/* ---- Shared airtime ---------------------------------------------------- */

/*
 * Jumps.  x1968_jumpsUsed counts the ground jump too, so a fighter with
 * max_jumps M has M-1 midair jumps.  The fraction of midair jumps left is
 * carried over, rounded down:
 *
 *   left_in = floor(left_out * (M_in - 1) / (M_out - 1))
 *   used_in = M_in - left_in
 *
 * Rounding down means the fraction can only shrink across any chain of
 * switches, so no sequence of switches yields more midair jumps than one
 * fighter staying in.  A grounded fighter has used nothing.
 */
tag_u32 tag_rules_map_jumps(int airborne, tag_u32 used_out, tag_u32 max_out,
                            tag_u32 max_in);

/* ---- Netplay announcement ---------------------------------------------- */

/*
 * Each modded client announces its player's roster in the two PAD button
 * bits no controller reports.  One bit per frame: TAG_PAD_NET_MARK marks
 * the first bit of a message, TAG_PAD_NET_DATA carries the bits, MSB first.
 * A message is TAG_NET_BITS long and repeats for the whole match.
 *
 *   version 2 | config 8 | ckind 3x5 | costume 3x3 | preload 16 |
 *   avail 12 | reserved 10 | crc8 8                        = 80 bits
 */
#define TAG_NET_VERSION 1u
#define TAG_NET_BYTES 10
#define TAG_NET_BITS (TAG_NET_BYTES * 8)
#define TAG_NET_CKIND_NONE 31u

typedef struct TagAnnounce {
    tag_u8 version;
    tag_u8 config;     /* hash of build, cooldown and flags */
    tag_u8 ckind[3];   /* TAG_CKIND_NONE for empty */
    tag_u8 costume[3];
    tag_u16 preload;   /* hash of the rosters this client preloaded */
    tag_u16 avail;     /* bit plan_index*3+slot: loaded on this client */
} TagAnnounce;

void tag_net_encode(const TagAnnounce* a, tag_u8 out[TAG_NET_BYTES]);
int tag_net_decode(const tag_u8 in[TAG_NET_BYTES], TagAnnounce* a);
/* Button bits to OR into the pad for message bit `phase`. */
tag_u32 tag_net_tx_bits(const tag_u8 msg[TAG_NET_BYTES], tag_u32 phase);

typedef struct TagNetRx {
    tag_u8 buf[TAG_NET_BYTES];
    tag_u8 count; /* bits collected, 0xFF = waiting for a mark */
    tag_u8 valid;
    TagAnnounce last;
} TagNetRx;

void tag_net_rx_reset(TagNetRx* rx);
/* Feed one frame's buttons; returns 1 when a complete valid message lands. */
int tag_net_rx_feed(TagNetRx* rx, tag_u32 buttons);

tag_u8 tag_crc8(const tag_u8* p, tag_u32 n);
tag_u32 tag_hash32(tag_u32 h, tag_u32 v);

/* ---- Rosters ------------------------------------------------------------ */

typedef struct TagRosterExtras {
    tag_u8 ckind[3];   /* slots 1..3 */
    tag_u8 costume[3];
} TagRosterExtras;

tag_u16 tag_roster_hash(const TagRosterExtras* r);
int tag_roster_empty(const TagRosterExtras* r);
/* Total order used to sort rosters identically on every netplay client. */
int tag_roster_cmp(const TagRosterExtras* a, const TagRosterExtras* b);

/*
 * Order in which extra roster entries are offered memory: round robin over
 * rosters, slot 1 of every roster before any slot 2.  `order` receives
 * roster*3 + (slot-1) codes; returns the count.
 */
int tag_rules_admission_order(int rosters, tag_u8* order);

/* Costume cycling for the CSS: next costume, wrapping. */
tag_u32 tag_rules_next_costume(tag_u32 cur, tag_u32 count);

#endif
