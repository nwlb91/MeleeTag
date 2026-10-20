#ifndef MELEETAG_TAG_H
#define MELEETAG_TAG_H

/*
 * Runtime state shared by the payload modules.
 *
 * Two lifetimes, deliberately separate:
 *
 * - TagGlobals (static payload memory at the top of the arena) holds what
 *   lives across scenes: the configuration, the rosters picked on the CSS,
 *   rosters learned from netplay opponents, the preload plan and the netplay
 *   transmit counter.  Slippi's rollback savestates do not cover this
 *   memory, so nothing in it may change gameplay inside a match.
 *
 * - TagMatch (HSD heap, user data of the manager GObj) holds everything that
 *   affects a match.  The HSD heap is part of Slippi's savestate, so a
 *   rollback restores it together with the fighters it points at.
 */

#include "tag_engine.h"
#include "tag_payload.h"
#include "tag_rules.h"

#define TAG_PORTS 4

/* Persistent per-character values of one fighter instance (tag_persist.c). */
#define TAG_PERSIST_MAX 12

typedef struct TagSaved {
    u8 kind;  /* FighterKind the values belong to, 0xFF = nothing saved */
    u8 count;
    u8 pad[2];
    u32 v[TAG_PERSIST_MAX];
} TagSaved;

enum TagSlotState {
    TAG_SLOT_EMPTY,    /* nothing assigned */
    TAG_SLOT_UNLOADED, /* assigned but not admitted to memory this match */
    TAG_SLOT_READY,
};

typedef struct TagSlot {
    u8 ckind;
    u8 costume;
    u8 state;          /* TagSlotState */
    u8 fresh;          /* a stock was lost since this slot last played */
    u8 form;           /* index into inst[] of the front fighter */
    u8 partner_alive;  /* Ice Climbers: Nana still alive this stock */
    u8 saved_valid;
    u8 pad;
    f32 partner_damage;
    u32 last_used;     /* match frame this slot was last switched out */
    /*
     * Resident fighters.  inst[0] is the CharacterKind's internal fighter,
     * inst[1] its extra one (Nana, or the other Zelda/Sheik form), NULL when
     * the slot has none or is not resident.
     */
    HSD_GObj* inst[2];
    TagSaved saved[2]; /* values while not resident */
} TagSlot;

typedef struct TagPlayer {
    u8 enabled;       /* tag mode for this player this match */
    u8 port;          /* controller port */
    u8 active;        /* active slot */
    u8 prev_dpad;     /* D-pad held last frame, for edge detection */
    u8 boost_used;    /* shared once-per-airtime boost token */
    u8 pad0;
    u8 feedback;      /* last TagDecision shown on the HUD */
    u8 feedback_timer;
    u16 cooldown;
    u8 announced;     /* netplay: announcement decoded */
    u8 pad;
    s32 falls;        /* main-fighter KOs seen so far */
    TagSlot slot[TAG_SLOTS];
    TagNetRx rx;
    TagAnnounce announce;
} TagPlayer;

#define TAG_HUD_WIDTH 22 /* characters per HUD line */

enum TagPhase {
    TAG_PHASE_ROSTERS, /* netplay: waiting for every announcement */
    TAG_PHASE_ACTIVE,
    TAG_PHASE_OFF,     /* tag disabled for this match */
};

typedef struct TagMatch {
    u32 magic;
    u32 frame;
    u8 netplay;
    u8 phase;
    u8 create_cursor;  /* round-robin pre-creation progress */
    u8 hud_ready;
    HSD_GObj* gobj;
    HSD_Text* hud;
    int hud_line[TAG_PORTS];
    char hud_last[TAG_PORTS][TAG_HUD_WIDTH + 1];
    TagPlayer pl[TAG_PORTS];
} TagMatch;

#define TAG_MATCH_MAGIC 0x4D544D54u /* 'MTMT' */

typedef struct TagGlobals {
    TagConfig* cfg;
    /* Local versus CSS: one roster per door (= controller port). */
    TagRosterExtras css[TAG_PORTS];
    /* Online CSS: the local player's roster. */
    TagRosterExtras net_local;
    /* Rosters announced by netplay opponents in the last online match. */
    TagRosterExtras learned[TAG_PORTS - 1];
    u8 learned_count;
    /* Preload plan from the last registration (tag_preload.c). */
    u8 plan_valid;
    u8 plan_netplay;
    u8 plan_rosters;
    TagRosterExtras plan_roster[TAG_PORTS];
    u8 plan_owner[TAG_PORTS];      /* local: port of plan_roster[i] */
    u8 plan_admit[TAG_PORTS][3];   /* 1 = files registered for that entry */
    u16 plan_hash;                 /* netplay: hash of the rosters preloaded */
    u8 loading_match;              /* inside the match load hook */
    u8 sound_admit[TAG_PORTS][3];  /* 1 = sound bank also fits */
    u16 plan_avail;                /* bit r*3+k: loaded at match start */
    /* Netplay transmit. */
    u8 net_tx;
    u8 net_msg[TAG_NET_BYTES];
    u32 net_phase;
    /* CSS. */
    u8 css_human[TAG_PORTS];
    HSD_Text* css_text;
    int css_line[TAG_PORTS];
    u8 css_prev_flick[TAG_PORTS];
    /* Current match, NULL outside one. */
    TagMatch* match;
} TagGlobals;

extern TagGlobals tag_g;
extern TagConfig tag_config;

/* tag_init.c */
void tag_init(void);
u8 tag_config_hash(void);
const char* tag_char_abbrev(int ckind);
int tag_ckind_valid(int ckind);
int tag_num_costumes(int ckind);

/* tag_preload.c */
int tag_preload_char_ready(int ckind, int costume);
int tag_preload_kirby_ready(int ckind, int kirby_costume);
void tag_preload_lazy_init(TagMatch* m);
u16 tag_preload_plan_hash(void);
void tag_preload_request_refresh(void);

/* tag_match.c */
TagMatch* tag_match_current(void);
int tag_fighter_count(void);

/* tag_switch.c */
void tag_switch_view(HSD_GObj* gobj, TagFighterView* v);
int tag_switch_entry(TagPlayer* p);
int tag_switch_perform(TagMatch* m, TagPlayer* p, int slot, int entry);
int tag_switch_make_resident(TagMatch* m, TagPlayer* p, int slot, int cap);
void tag_switch_adopt_primary(TagMatch* m, TagPlayer* p);
void tag_switch_sync_form(TagMatch* m, TagPlayer* p);
void tag_switch_on_stock_lost(TagMatch* m, TagPlayer* p);
HSD_GObj* tag_slot_front(TagSlot* s);

/* tag_persist.c */
void tag_persist_save(HSD_GObj* gobj, TagSaved* out);
void tag_persist_restore(HSD_GObj* gobj, const TagSaved* in);
void tag_persist_fresh(HSD_GObj* gobj);
int tag_persist_boost_used(HSD_GObj* gobj);
void tag_persist_set_boost(HSD_GObj* gobj, int used);
void tag_persist_after_entry(HSD_GObj* gobj);
void tag_persist_switch_out(HSD_GObj* gobj);

/* tag_hud.c */
void tag_hud_create(TagMatch* m);
void tag_hud_update(TagMatch* m);

/* tag_net.c */
void tag_net_prepare(TagMatch* m);
void tag_net_frame(TagMatch* m);
void tag_net_stop(void);

#endif
