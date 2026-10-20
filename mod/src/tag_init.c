/*
 * Boot-time initialisation and small shared tables.
 */
#include "tag.h"

TagGlobals tag_g;

/* Three-letter names in CharacterKind order, for the CSS and HUD text. */
static const char* const tag_abbrev[TAG_CKIND_PLAYABLE] = {
    "FLC", /* Captain Falcon */
    "DK",  /* Donkey Kong */
    "FOX", /* Fox */
    "GW",  /* Mr. Game & Watch */
    "KBY", /* Kirby */
    "BOW", /* Bowser */
    "LNK", /* Link */
    "LGI", /* Luigi */
    "MAR", /* Mario */
    "MTH", /* Marth */
    "MT2", /* Mewtwo */
    "NES", /* Ness */
    "PCH", /* Peach */
    "PKA", /* Pikachu */
    "ICS", /* Ice Climbers */
    "JIG", /* Jigglypuff */
    "SAM", /* Samus */
    "YSH", /* Yoshi */
    "ZLD", /* Zelda */
    "SHK", /* Sheik */
    "FCO", /* Falco */
    "YL",  /* Young Link */
    "DOC", /* Dr. Mario */
    "ROY", /* Roy */
    "PCU", /* Pichu */
    "GNN", /* Ganondorf */
};

const char* tag_char_abbrev(int ckind)
{
    if (ckind >= 0 && ckind < TAG_CKIND_PLAYABLE) {
        return tag_abbrev[ckind];
    }
    return "---";
}

int tag_ckind_valid(int ckind)
{
    return ckind >= 0 && ckind < TAG_CKIND_PLAYABLE;
}

int tag_num_costumes(int ckind)
{
    int n;
    if (!tag_ckind_valid(ckind)) {
        return 1;
    }
    n = gm_GetNumCostumesForCKind(ckind);
    return n > 0 ? n : 1;
}

/*
 * Everything that must match between netplay clients for tag switching to
 * be deterministic: the payload build and the patched configuration.
 */
u8 tag_config_hash(void)
{
    extern TagPayloadHeader tag_payload_header;
    u32 h = 2166136261u;

    h = tag_hash32(h, tag_payload_header.build_id);
    h = tag_hash32(h, tag_g.cfg->cooldown_frames);
    h = tag_hash32(h, tag_g.cfg->flags & ~TAG_CFG_HUD);
    h = tag_hash32(h, tag_g.cfg->max_fighters);
    return (u8) (h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24));
}

static void tag_roster_clear(TagRosterExtras* r)
{
    int i;
    for (i = 0; i < 3; i++) {
        r->ckind[i] = TAG_CKIND_NONE;
        r->costume[i] = 0;
    }
}

void tag_init(void)
{
    int i;

    tag_g.cfg = &tag_config;
    if (tag_config.magic != TAG_CONFIG_MAGIC) {
        tag_config.cooldown_frames = TAG_DEFAULT_COOLDOWN;
        tag_config.flags = TAG_CFG_HUD | TAG_CFG_NETSYNC;
        tag_config.max_fighters = TAG_DEFAULT_MAX_FIGHTERS;
    }
    if (tag_config.cooldown_frames > TAG_MAX_COOLDOWN) {
        tag_config.cooldown_frames = TAG_MAX_COOLDOWN;
    }
    if (tag_config.max_fighters < TAG_MIN_FIGHTERS ||
        tag_config.max_fighters > TAG_MAX_FIGHTERS)
    {
        tag_config.max_fighters = TAG_DEFAULT_MAX_FIGHTERS;
    }
    for (i = 0; i < TAG_PORTS; i++) {
        tag_roster_clear(&tag_g.css[i]);
    }
    tag_roster_clear(&tag_g.net_local);
    tag_g.learned_count = 0;
    tag_g.plan_valid = 0;
    tag_g.match = NULL;
    tag_g.css_text = NULL;
}
