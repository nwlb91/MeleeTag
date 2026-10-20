# MeleeTag design

Everything engine-specific here was read from the matched decompilation of
NTSC 1.02 (`third_party/melee`, doldecomp/melee). Function names are the
decomp's; addresses come from its `config/GALE01/symbols.txt`. The payload
is compiled against the decomp's own headers, so every struct offset used is
the decomp's (its layout static asserts pass under the cross compiler), and
`mod/src/tag_switch.c` static-asserts every motion-state number the rule
table uses.

## Layout

```
mod/
  payload.ld            one output section at 0x80A00000
  include/tag_payload.h image header, hook record, config (shared with patcher)
  src/boot_entry.S      entry from the HSD_OSInit branch
  src/boot.c            relocator + hook installer (runs once)
  src/payload_header.c  header, hook table, default config
  src/tag_rules.c       pure rules (also built for the host tests)
  src/tag_switch.c      the switch, fighter objects, Ice Climbers partner
  src/tag_persist.c     per-character state, boosts, fresh-stock reset
  src/tag_preload.c     preload admission, sound banks, lazy init
  src/tag_match.c       match hook, per-frame manager
  src/tag_net.c         Slippi netplay announcement and verdict
  src/tag_css.c         roster picking on the CSS
  src/tag_hud.c         in-match slot line
patcher/                ISO patcher (C, Linux + Windows), MeleeTag.bat
tools/                  symbol script, payload packer, Slippi conflict check
tests/                  host, qemu-ppc and patcher tests
```

## Getting code into the game

**Where the code lives.** The patcher adds one DOL text section loaded at
`0x80A00000`. That address is free during boot but becomes heap later, so
the first thing the payload does is move itself: `HSD_OSInit`'s first
instruction (`0x80375304`) is replaced by a branch to `tag_boot_entry`;
`tag_boot` puts the original instruction back, copies the image to the top
of the arena, applies the relocation table, lowers the arena top with
`OSSetArenaHi`, installs the hooks and calls `tag_init`. `HSD_OSInit` then
runs unmodified and builds every heap below the payload.

Why the top: Slippi's rollback saves fixed memory ranges (DOL data/bss,
`0x8065C000-0x8071B000`, and the HSD heap between `hsd_heap_next_arena_lo`
and `_hi`). Moving the arena bottom would shift those; taking memory from
the top only shrinks the HSD heap's upper end. The payload's own static data
is therefore *not* saved by rollback, which is why nothing in it may change
during a match (see Netplay).

If less than 8 MiB of arena would remain, the boot code restores the
original instruction and returns: the game runs vanilla.

**Relocation.** `ld -q` keeps relocations; `tools/mkpayload.py` keeps the
ones that must change when the image moves: absolute references into the
image (`ADDR32`, `ADDR16_LO/HI/HA`) and relative branches *out* of it to
engine code (`REL24`). Everything else is position independent.

**Hooks.** Function-entry detours: the first instruction of each hooked
function becomes a branch to a C handler with the same signature; the
handler calls the original through a two-word trampoline (displaced
instruction + branch back). The patcher records each original instruction
from the user's DOL and refuses PC-relative ones; the build refuses any site
Slippi's codes write (`tools/slippi_conflicts.py` against the netplay,
playback, console and bootloader code lists). Hooked:

| Function | Why |
| --- | --- |
| `fn_8016E730` | versus match load: plan, sound banks, lazy init, manager |
| `lbDvd_80017960` | a scene registers its preload set: add extra characters |
| `lbAudioAx_8002785C` | match sound bank load: add extra characters' banks |
| `mnCharSel_Scene_OnEnter` / `OnFrame` | roster picking UI |
| `PADRead` | netplay announcement bits |

## The switch

### Model

`StaticPlayer` (pl/player.h) holds `player_entity[2]` and `transformed[2]`:
the engine's view of a player is the fighter in `entity[transformed[0]]`
plus, for Zelda/Sheik and the Ice Climbers, a second one. Everything that
looks for "the player's fighter" (stock loss in `ftCo_800D34E0`, respawn in
`Player_80032070`, the HUD, the offscreen bubble, CPUs) goes through it.

MeleeTag keeps that model exactly: the active slot's fighters are in the
StaticPlayer (`entity[0]` front, `entity[1]` partner), with `transformed`
normalised to `{0,1}` and `ckind`/`costume_id`/`flags.b2` matching the
active character. Fighters of inactive slots are kept out of it, in the
engine's `Sleep` state (`ftCo_800BFD04`), the state the engine keeps Sheik
in while Zelda plays: invisible, no hurtboxes, no camera box, and every
proc that matters checks `is_sleeping` (e.g. `Fighter_procPlayer`), so a
sleeping fighter's character state simply stays as it was.

### Transfer

`tag_transfer` is the engine's Zelda/Sheik transform (`ftCommon_8007EFC8`)
with source and destination explicit, field for field:
`Fighter_UnkInitReset_80067C98` and the velocity reset on the incoming
fighter, then position, facing, damage (and `Player_SetHPByIndex`), all
velocities, ground state, controller history, last attacker and hit
bookkeeping, walljump, shield health, bury/stamina fields, timed
intangibility (`ftColl_8007B7FC` with the remaining frames, blink colour
anim), invincibility (`ftColl_8007B760`), star/mushroom/metal/bunny
hood/flower effects with their timers, scale, attributes, camera box,
collision data (`ft_800849EC`), held item (`ftpickupitem_80094818`,
`it_8026B9A8`), then `Sleep` for the outgoing fighter. Differences:

- no transformation colour flash (`ftCo_800C0200(dst, 9)`) and no crowd
  gasp (`un_80322314`);
- also copied: knockback velocity, fast-fall, ledge-regrab cooldown, the
  post-hitstun and grab-release timers, time since hit, tether flag;
- jumps go through the airtime rule below instead of being copied;
- the outgoing fighter's held item pointer is cleared after the handover.

The incoming fighter then enters its state through the engine's own Enter
function (`ft_8008A2BC` Wait, `ftCo_800D638C` SquatWait, `ftCo_Fall_Enter`,
`ftCo_80090780` DamageFall), which rebuilds its callbacks, physics and
collision. Velocities saved before the call are written back after it so
momentum survives exactly (Fall_Enter would clamp air drift); from the next
frame the incoming character's own friction and drift apply. ECB size
differences are resolved by the engine's collision on the next frame, as
when an animation changes the ECB.

### Shared airtime

Jumps: `x1968_jumpsUsed` counts the ground jump, so `max_jumps - 1` midair
jumps exist. The fraction of midair jumps left carries over, rounded down:
`left_in = floor(left_out * (Min - 1) / (Mout - 1))`. Rounding down means the
fraction never grows across any chain of switches (the host tests try every
four-switch chain over all jump counts), so no team gets more midair jumps
than its best single character. Airborne fighters always count the first
jump spent (`ftCommon_8007D5D4` sets 1).

Boosts: several specials give a one-time boost per airtime, each with its
own flag that the character's landing code clears (`ftCo_Landing_Enter`
lists most). The player has one token per airtime: set when the active
fighter's flag shows a boost used while airborne, cleared on landing; each
incoming fighter's flags are written from the token
(`tag_persist_set_boost`). Helpless fall, airdodge, and landing lag cannot
be escaped because those states are never switchable.

### When a switch is allowed

`tag_rules_entry` (pure, unit tested) decides from values copied out of the
fighter. Global: no hitlag (`x2219_b5`), no hitstun (`x221C_b6`), not
sleeping, input enabled (`input_disabled` covers the countdown and the end
of the match), not grabbing or grabbed (`victim_gobj`, `x1A5C`), not
holding a hammer (`ftCo_800C5240`). Then by state, matching the engine's
own IASA functions:

| States | Rule | Source |
| --- | --- | --- |
| Wait, Walk*, Turn, Run, SquatRv, Ottotto(Wait) | always | their IASA functions run the full Wait set |
| Squat, SquatWait | always, incoming crouches | `ftCo_Squat_IASA` |
| JumpF/B, JumpAerialF/B, Fall* | always, incoming falls | |
| DamageFall | always, incoming tumbles | `ftCo_DamageFall_IASA` |
| Damage* (flinch) | after hitstun | `ftCo_Damage_IASA` → Wait/Fall IASA |
| DamageFly* | after hitstun, incoming tumbles | `ftCo_DamageFly_IASA` → DamageFall IASA |
| Attack11..13, Attack100End, AttackDash, tilts, smashes | `allow_interrupt` set | e.g. `ftCo_AttackHi3_IASA` |
| AttackAir* | `allow_interrupt` set, incoming falls | `ftCo_AttackAir*` `DO_IASA` |
| Landing, LandingFallSpecial | `mv.co.landing.allow_interrupt` and past `normal_landing_lag` | `ftCo_Landing_IASA` |
| everything else | never | |

`allow_interrupt` is only cleared by the Enter functions of states that use
it, so a set flag in any other state (including every character-specific
state) can be stale; those states are never switchable. With the Ice
Climbers active, Nana must also be in a free state (not hit, launched,
knocked down, helpless, grabbing or grabbed), so switching never rescues her.

Death: the dead states, the respawn platform (`Rebirth`, `RebirthWait`) and
`Sleep` are never switchable; the blast-zone check runs in the fighter's
own procs after physics, so a fighter is either in bounds or dead by the
time the manager runs.

### Entities

- **Fired projectiles** keep their owner (the now sleeping fighter), so hits
  and kills credit the right player; the fighter stays allocated while it
  owns items when possible (eviction prefers fighters without live items).
  If one must go anyway, `Fighter_Unload_8006DABC` detaches its items through
  each item's own owner-removed callback (`it_8026B7F8`).
- **Callbacks into the owner.** Before a fighter leaves, MeleeTag runs the
  `death1`/`death3` callbacks the death path would run (`ftCo_800D331C`):
  they detach whatever an earlier action left behind that would otherwise
  call back into a sleeping fighter: Link's returning boomerang
  (`ftLk_SpecialS_RemoveBoomerang1`, otherwise `ftLk_SpecialS2_Enter` would
  wake a sleeping Link), Peach's parasol, Popo's belay, Mewtwo's held ball,
  tether hooks. Kirby is the exception: his callbacks remove the copied
  ability, which belongs to the slot.
- **Held items** move to the incoming fighter as in the transform; items
  worn (`x197C`, `x1980`) too. Nana's held item is destroyed as on death.
- **Sounds**: `ft_80088C5C` stops the outgoing fighter's loops and voices.
- **Reflectors, hitboxes, grabs** are state; the outgoing fighter's state
  ends with the Sleep transition (`Fighter_ChangeMotionState` runs the
  state's exit callback and clears per-state callbacks).

### Partners

Zelda/Sheik: a slot holds both fighters, as the engine does; the slot
remembers which form was in front (the engine's own down-B swaps
`transformed`, read back each frame). Its back form gets input enabled when
the slot comes in (vanilla enables both at GO; ours may be created later).

Ice Climbers: Nana leaves with Popo if alive (her damage recorded, her item
dropped) and returns at Popo's position, ground state and velocity, with
fresh follower AI (`ftCo_800A101C`, as on respawn). A dead Nana stays asleep
until the next stock, when the engine's respawn (`Player_80032070`) wakes her
or the fresh-stock reset marks her alive.

### Fighter objects and memory

A switch never loads data: all extra characters are loaded before the match
(next section). It may create a fighter object (`Fighter_Create` with the
"has transformation" flag, which starts it asleep, exactly how the engine
creates Sheik); the object's procs are queued after the manager's, so the
new fighter runs the same frame. To make switches pure transfers, the
manager pre-creates sleeping fighters for roster slots, one per frame, up
to `max_fighters` (default 8, which the unmodified game already reaches in a
four-player Zelda/Ice Climbers match). Over the cap, the least recently used
inactive slot's fighters are destroyed (their per-character state saved,
`tag_persist.c`) and recreated on demand; a switch may briefly hold two
above the cap.

`HSD_GObjFree` runs `Fighter_Unload_8006DABC`, which ends with
`Player_80031FB0` clearing the StaticPlayer entity the fighter *would*
occupy and possibly the player state; MeleeTag saves and restores those
around the call since an inactive fighter occupies nothing.

### Per-character state

While a slot's fighter is resident, its state needs no handling. When it is
destroyed, these fields are saved and restored (audit of every FighterVars
struct, fp+0x222C):

| Character | Fields | Reset by OnDeath |
| --- | --- | --- |
| DK | `dk.x222C/x2230` punch swings | yes |
| Samus | `ss.x2230` charge | yes |
| Mewtwo | `mt.x2234` shadow ball charge | yes |
| Sheik | `sk.x0` needles | yes |
| Bowser | `kp.x222C/x2230` fire fuel | yes |
| Mario, Doc | `mr.x222C/x2230` pill colours | yes |
| G&W | judge, chef, bucket fill (`x2238`) and damage | bucket fill: no |
| Peach | `attacks4_motion_id` | yes |
| Kirby | hat kind (re-granted with `ftKb_SpecialN_800F1BAC`) and copied resources (`x68`, `x6C`, `x84`, `x88`, `x9C`, `xA8`, `xB4`, `xBC`, `xD4`, `xD8`) | yes |

Everything else is an item/effect pointer, a per-action value or a boost
flag. `Fighter_UnkInitReset_80067C98` clears Kirby's `death1/3` callbacks
when he comes in, so they are re-armed with `ftKb_SpecialN_800F1BAC` (same
kind: no model change).

### Stock loss

Every KO of the main fighter bumps `falls[0]` (`ftCo_800D34E0`), stock or
time match. When it changes, every inactive slot is marked fresh; when it
next comes in, after any saved values are restored, MeleeTag runs the
engine's reset for a fighter leaving play and respawning: the `death3`/
`death1` cleanups, then the character's `OnDeath` (what `Fighter_Spawn`
calls). Nana is marked alive at 0%. What vanilla keeps across stocks, the
reset keeps. The active slot goes through the engine's own death and respawn.

## Loading extra characters

Melee's preload cache (lb/lbdvd.c) loads a scene's declared files in the
background into AllM (main RAM, `0x64B400` bytes) and AllA (ARAM,
`0x96C800`). Before each file it releases unneeded entries and compacts the
heap, so the heap overflows exactly when the registered files' rounded sizes
(plus one `HSD_Archive` header for archive types) exceed its size; the
allocator then asserts. Loads during a match are impossible: compaction
would move archives in use, and rollback does not cover these heaps.

`lbDvd_80017960` (registering a scene's set) is wrapped: after the vanilla
registration, each extra roster character is registered with the engine's
own functions (`Player_80031CB0`; `Player_80031D2C` for every Kirby hat it
implies), and the cache entries are rolled back if the registered total no
longer fits either heap with a 64 KiB margin, or if the 80-entry table
could run out. File sizes come from the disc's file table
(`lbFile_8001634C`). Order: every roster's first extra, then every second,
then every third. On the CSS this runs whenever a roster changes; the match
scene's own registration (with the stage) decides the final set.

Sound: `lbAudioAx_8002785C` is wrapped; each admitted character's banks are
added if the total of wanted banks plus non-evictable loaded ones stays
within the sound heap (the same sum `lbAudioAx_80027168` asserts on). A
character without its bank is not available (an unloaded bank is unsafe).

At match load, everything the engine would initialise lazily on first use is
done before frame 1 (fighter archive, effects, costume and animation table,
as `Fighter_Create` would; Kirby copy data with `Player_80031DA8` for every
character x Kirby costume pair, as `fn_8016E124` does for the primaries),
waiting for any file still loading. A slot is available only if all of its files are
registered and its bank fits; anything that is not is shown as `-`.

## The per-frame manager

A GObj created after `fn_8016E730` with one proc at priority 0 on p_link 0.
Procs run by priority, then p_link, so it runs before every fighter proc and
after the pad status was renewed. Each frame, per tag player:

1. read the D-pad from `HSD_PadMasterStatus` and detect fresh presses with
   its own previous-frame copy (the game status' edge detection cannot be
   used once the D-pad is stripped);
2. strip the D-pad from `HSD_PadGameStatus` (fighters read their buttons
   there), which removes taunts;
3. tick the cooldown, track KOs, the Zelda/Sheik form and the boost token;
4. on a fresh press, decide (`tag_rules_decide`) and switch.

All of it lives in the HSD heap (the GObj's user data), so a Slippi rollback
restores it with the fighters; the proc does nothing while fighters are
paused.

## Netplay

Slippi runs every client in lockstep with rollback on the shared inputs;
the payload's static memory is not in the savestate. So:

- Match state (the manager, fighters) lives in the HSD heap; decisions read
  only that state and the frame's inputs.
- Static data (config, CSS rosters, learned rosters) does not change during
  a match except the learned rosters, which are only read at the next match.
- Rosters travel in PAD button bits `0x4000`/`0x8000`, which no controller
  sets and Slippi transmits. `PADRead` (whose result Slippi sends as the
  local input) ORs in one bit per frame, indexed by the manager's frame
  counter so multiple reads per frame or rollbacks never skip a bit; the
  manager feeds every human's bits to a decoder and strips them before any
  game code reads them. The message is 80 bits with a start mark and CRC-8:
  roster (3 x 5-bit character + 3-bit costume), a hash of the build and
  settings, a hash of the rosters the client preloaded, and 12 bits of
  which preloaded slots are fully in memory on that client.
- The verdict (`tag_rules_net_decide`, pure and tested) is computed by
  every client from the same confirmed inputs: tag is on iff every client
  has the same build/settings hash and preloaded exactly the announced
  rosters (sorted canonically, so port order does not matter); a slot is
  available iff every client has it in memory. Until the verdict the D-pad
  is stripped for every human on every client; after a negative verdict the
  match is vanilla. If not every announcement arrives within 480 frames, tag
  is off.
- Each client then remembers the opponents' rosters for the next match's
  preload. The first game against a roster cannot have it preloaded.

## Text

CSS text uses the CSS's own canvas (font 0); HUD text uses a canvas on the
HUD's camera (font 2, gx link 11, as the end-of-match score does). Scene
text heaps are small (`0x2400` on the CSS, `0x4800` in a match) and run out
fatally, so lines are created once at full width and only rewritten in place
(SisLib then never allocates), and nothing is created when less than
1.5 KiB (CSS) or 2.25 KiB (match) of the text heap is free.
