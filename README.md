# MeleeTag

Tag-team switching for Super Smash Bros. Melee (NTSC 1.02): each player
brings four characters into a match, one per D-pad direction, and swaps
between them instantly during play. One stock pool, one damage meter, one
player; the characters take turns being on screen.

Works in Dolphin and in Slippi (local play and Slippi online, when every
player uses an ISO patched the same way).

> **Status.** The mod builds, links against the retail game's symbols, and
> passes the automated tests below (rule logic, the boot code executed on a
> PowerPC emulator, the ISO patcher on synthetic disc images). It has **not
> been played in-game yet**: no game image or GameCube emulator was
> available while it was written. Treat the first sessions as testing; the
> checklist in [docs/TESTING.md](docs/TESTING.md) is the place to start.

## Installing

You need an unmodified NTSC 1.02 Melee ISO (game ID `GALE01`, revision 2).
The patcher checks the game executable against the retail one and refuses
anything else; your ISO is never modified.

**Windows:** put `MeleeTag.bat` and `meleetag-patch.exe` in the same folder,
then drag your ISO onto `MeleeTag.bat`. A new file named
`<your ISO name>.MeleeTag.iso` appears next to the original.

**Linux / macOS:** `./meleetag.sh "Super Smash Bros. Melee.iso"`

Load the `.MeleeTag.iso` in Dolphin or Slippi like any other Melee ISO.

## Controls

### Character select screen

Pick your main character the normal way: that is your **Up** slot.

| Action | Result |
| --- | --- |
| Point the hand at a character, flick **C-stick Right / Down / Left** | That character goes to the D-pad slot of the same direction |
| Flick the same direction again on the same character | Next costume (Zelda continues into Sheik and back) |
| Flick a direction while pointing at no character | Clears that slot |
| Flick **C-stick Up** | Clears all three extra slots |

Each door shows its roster, e.g. `R:FOX D:MAR.2 L:SHK` (`.2` = second
costume). Rosters stay set between matches.

### In a match

Press a **D-pad direction** to switch to that slot's character. Up is the
character you started with.

- The switch happens on the frame you press, as long as your character can
  act (see *When you can switch*). There is no animation, pause or
  invincibility.
- A switch starts a shared cooldown, 30 frames by default.
- A press only counts on the frame you press it: holding a direction never
  repeats, and a press that is refused (cooldown, busy, empty slot) is
  dropped, not saved for later. Pressing the current slot does nothing and
  costs no cooldown.
- D-pad taunts are disabled for players using tag mode.
- Every human player switches independently. CPUs play their main
  character only.

The HUD shows a line next to each player's damage meter:

```
FOX fco mar shk 12
```

The active slot is in capitals, the others follow in Up, Right, Down, Left
order; `-` marks an empty slot or one that could not be loaded this match;
the number is the cooldown left. After a refused press you briefly see
`BUSY` (character can't act), `WAIT` (cooldown) or `N.A.` (slot not
available).

## When you can switch

The rules follow the game's own interrupt windows, not just "not in
hitstun". You can switch out of:

- standing, walking, running, turning, crouching, teetering;
- jumping, double jumping, falling (including fast-falling);
- tumble, once hitstun is over (the incoming character keeps tumbling, so it
  still has to tech or get knocked down on landing);
- flinch and launch animations, once hitstun is over;
- ground attacks and aerials, once their interrupt window (IASA) has started;
- normal landing, once its landing lag is over.

You cannot switch during: hitlag, hitstun, shield, rolls, spot/air dodges,
helpless fall or its landing, aerial landing lag, jump squat, dash, run
turnaround and run brake, ledge, grabs and throws (either side), knockdown
and techs, dizzy, sleep, bury, item-throw and hammer states,
character-specific moves (all specials), the countdown, death and the
respawn platform. With the
Ice Climbers active, Nana must also be free (not hit, launched, knocked
down, grabbing or grabbed).

The incoming character enters the matching neutral state (standing,
crouching, falling or tumbling) at the same position, facing the same way,
with the same momentum.

## What carries over

| State | Belongs to | Notes |
| --- | --- | --- |
| Stocks, damage, position, facing, momentum, grounded/airborne, team, last attacker (kill credit), staling | the player | moves to the incoming character |
| Shield health, respawn/ledge intangibility, invincibility, metal box, bunny hood, mushrooms, flower, held item, ledge-regrab and grab-release timers | the player | keep their remaining time; switching neither adds nor removes any |
| Midair jumps | the player | the fraction of midair jumps left carries over, rounded down (Fox with his double jump → Kirby with all five; Kirby with one of five left → Fox with none); no switch chain can create jumps |
| Wall jump, tether use | the player | carried over as is |
| Once-per-airtime boosts of specials (Mario/Doc cape and tornado, Peach float and air Toad, Marth/Roy side-B, G&W side-B, Ice Climbers neutral-B, Mewtwo side-B, Luigi cyclone, Kirby's copies of these) | the player | one per airtime for the whole team: once any of your characters uses one, the others start that airtime with theirs used |
| Zelda/Sheik form | the slot | the slot comes back in the form it left |
| Ice Climbers: whether Nana is alive, her damage | the slot | Nana comes back where Popo appears; a dead Nana stays out until your next stock |
| Kirby's copied ability and its charges | the slot | |
| Samus, DK, Mewtwo charges, Sheik's needles | the slot | kept; nothing charges while a slot is out |
| Bowser's fire breath fuel | the slot | does not refill while out |
| G&W bucket, judge and chef cycles, Doc's pill colours, Peach's smash item | the slot | |
| Current move, hitboxes, grabs, animation, sounds | the action | ended when the character leaves |

**Inactive characters are frozen**: no charging, refilling or timers run for
a slot that is not on screen. Timers that belong to the player (cooldown,
intangibility, item effects) keep running on whoever is in.

**Losing a stock** resets all four slots the way the game resets a character
on respawn: charges are emptied, Kirby loses his ability, Nana returns at 0%.
Things the game keeps across stocks (G&W's bucket) are kept.

**Projectiles** you fired before switching keep flying and still count as
yours (hits, kills). Link's and Young Link's boomerang is recalled as if
Link had been KO'd, since it would otherwise try to return to him.

## Settings

The patcher takes options; with the `.bat`, set them in `MELEETAG_OPTIONS`
first (e.g. `set MELEETAG_OPTIONS=--cooldown 45`) or run
`meleetag-patch.exe` from a command prompt.

| Option | Default | Meaning |
| --- | --- | --- |
| `--cooldown N` | 30 | frames between switches (0 to 600) |
| `--no-hud` | HUD on | hide the in-match slot line |
| `--max-fighters N` | 8 | fighter objects alive at once, all players together (4 to 12) |
| `--no-netplay-sync` | sync on | never enable tag in Slippi online matches |
| `--verify` | | only check the ISO, write nothing |

`--max-fighters` trades memory for speed. Each extra character of each
player is kept loaded (asleep) when there is room, so switching to it costs
nothing; when there is no room, the least recently used inactive character
is removed and recreated on its next switch (still the same frame, a few
milliseconds of extra work). 8 is the number of fighters the unmodified
game already runs in a four-player match of Zeldas or Ice Climbers.

## Slippi online

- Every player needs an ISO patched with the same MeleeTag build and the
  same settings. Otherwise tag stays off for the match (everyone plays their
  main character) and nothing else changes.
- Characters cannot be loaded once a match has started, and Slippi gives no
  way to exchange rosters before it. So rosters are exchanged during each
  match, and each client preloads the rosters it learned last time. The
  **first game against an opponent, or the first game after anyone changes
  their roster, is played without tag** (the HUD shows `TAG SYNC`, then
  nothing). From the next game on, tag works.
- Tag turns on a little after the start countdown (the exchange needs about
  80 frames of input after Slippi's start sync).

## Memory

Melee loads characters only before a match, into fixed-size memory areas.
MeleeTag adds extra characters to that load one by one, with the exact file
sizes from the disc, keeping each only if everything still fits (fighter
data, costume, animations, effects, Kirby's copy hats, and sound banks).
The first extra of every player is offered memory before anyone's second.
A character that does not fit shows as `-` for that match. How many fit
depends on the stage and on which characters are involved (and has not been
measured in-game yet); the HUD shows the outcome for each match.

## Limitations

- Not yet tested in-game (see Status). Text positions on the CSS and HUD
  were placed from the game's coordinates and may need adjusting.
- Versus mode only (local and Slippi online). Other modes are unchanged.
- Slippi replays of tag matches do not play back correctly (the replay
  viewer does not know the rosters to preload).
- The stock icons and the results screen show the character that was
  active at the time, not the roster.
- Switching is not possible on the ledge or while shielding; let go or drop
  shield first.
- All specials are character-specific states and therefore never
  switchable, even ones with early interrupt windows.
- Every player needs a controller with a working C-stick to set a roster.

## Building from source

Requirements: `powerpc-linux-gnu-gcc`/`ld`, Python 3, a host C compiler;
optional `x86_64-w64-mingw32-gcc` (Windows build) and `qemu-ppc` (boot
test).

```
git submodule update --init
make            # build/payload.bin, build/meleetag-patch, build/meleetag-patch.exe
make test       # unit, emulator and patcher tests
make dist       # dist/ with the patcher, .bat, shell script and README
```

How it works is in [docs/DESIGN.md](docs/DESIGN.md); what is tested and how
to check the rest in-game is in [docs/TESTING.md](docs/TESTING.md).

The engine knowledge comes from the matched decompilation at
[doldecomp/melee](https://github.com/doldecomp/melee) (`third_party/melee`),
and Slippi compatibility is checked against
[project-slippi/slippi-ssbm-asm](https://github.com/project-slippi/slippi-ssbm-asm)
(`third_party/slippi-ssbm-asm`).
