# Testing

## What has been run

`make test` runs all of these; they pass on the current tree.

| Test | What it exercises | How |
| --- | --- | --- |
| `tests/test_rules.c` (~50,000 checks) | the switch decision, fresh-press detection (holding never repeats, rejected presses are not queued, same slot costs nothing), cooldown, the full actionability table including every "never" state class, Ice Climbers partner safety, the airtime mapping (every 4-switch chain across all jump counts never increases the fraction of midair jumps), the netplay codec (round trip, every single-bit error caught, decoding mid-stream after Slippi's zeroed start frames), roster hashing and ordering, the netplay verdict (first game off, learned rosters on, per-slot availability agreed by all clients, mismatched settings off, changed roster off) | host C, compiled from the same `mod/src/tag_rules.c` as the payload |
| `tests/qemu/boot_test.c` | the built `build/payload.bin` booting: entry, copy to the top of the arena, every relocation (absolute and branch-to-engine), hook branches and trampolines at the real hook addresses, restored boot site, `tag_init` running in the copy, relocated code (jump tables included) giving correct results; and the low-memory path leaving the game vanilla | PowerPC binary under `qemu-ppc` with GameCube RAM mapped at `0x80000000` and stub `OSGetArenaHi/Lo`, `OSSetArenaHi`, cache functions and `memcpy` at their retail addresses |
| `tests/test_patcher.py` | the patcher on synthetic disc images: DOL rewritten in place; the retail layout (file table right after the DOL), where the file table moves; packed files, where the DOL moves; no room (refused, nothing written); untouched game files, apploader and existing DOL sections; boot branch; recorded hook instructions; settings; refusal of PAL/rev 1, a modified DOL, a PC-relative hook site, an already patched ISO, bad options; `--verify` | Python driving the built patcher |
| build checks | every motion-state number in the rule table static-asserted against the decomp enum; the payload links against the retail symbol map; none of the 7 patched words is written by any Slippi code list (netplay, playback, console and its optional codes, bootloader) | `make` |

## What has not been run

Nothing has run inside Melee. No ISO or GameCube emulator was available
while writing this, so everything that touches the engine at run time is
checked only by compiling against the decomp's headers and by reading the
decomp. The list below is the in-game test plan; it covers every behaviour
the mod is meant to have. Run it in Dolphin (or Slippi Dolphin) with a
patched ISO, debug mode off.

Useful setup: Training is not supported, so use Versus with 2 human
controllers (or one controller and a second one idle at port 2), stock
mode, items off unless noted. Set the cooldown to the default.

### Boot and CSS

1. The game boots to the title screen; Versus, Training, 1P modes, the
   options menu all work as in vanilla.
2. On the Versus CSS, hover Fox, flick C-stick right: the door shows `R:FOX`.
   Flick right again: `R:FOX.2`. Repeat through all costumes and back.
3. Hover Zelda, flick down repeatedly: Zelda's costumes, then `SHK`
   costumes, then Zelda again.
4. Flick left over empty space: `L:-`. Flick up: all three cleared.
5. Set rosters for two ports; go to a match and back; rosters are kept.
6. CSS text sits under the icons near each door and does not overlap
   anything important (adjust `tag_css.c` constants otherwise).

### Basic switching

7. In a 1v1, the HUD line next to each damage meter reads e.g.
   `FOX fco mar shk`. During the countdown nothing switches.
8. Standing: D-pad right switches instantly to slot 2 at the same spot and
   facing; the HUD shows the cooldown counting down from 30.
9. Press D-pad left during the cooldown: `WAIT`, no switch, and nothing
   happens when the cooldown ends.
10. Hold D-pad down for several seconds: exactly one switch.
11. Press D-pad up while Up is active: nothing, no cooldown shown.
12. An empty slot shows `-`; pressing it shows `N.A.`.
13. D-pad up no longer taunts for tag players; a player without a roster
    still taunts.
14. Two players switching at the same time both work.

### Actionability

15. Running, walking, turning, crouching (incoming crouches), teetering:
    switch works. Dashing, run brake, run turnaround, jump squat: `BUSY`.
16. Jumping, double jumping, falling, fast-falling: switch works; the
    incoming character keeps the momentum (watch a fast run-off-ledge or a
    full-speed fast fall).
17. Shield up: `BUSY`. Roll, spot dodge, air dodge: `BUSY`.
18. Up-B into helpless fall: `BUSY` through the fall and the landing lag.
19. Aerial into aerial landing lag: `BUSY` until the lag ends; a normal
    landing: `BUSY` for the first landing frames, then works.
20. Ground attack: `BUSY` until its interrupt window, then works (try a
    tilt with a long ending, e.g. Marth down tilt / Fox up tilt).
21. On the ledge: `BUSY`. Grabbing or being grabbed: `BUSY`.
22. Getting hit: `BUSY` during hitlag and hitstun; tumbling after hitstun:
    switch works and the incoming character still tumbles (lands in
    knockdown or techs). Knockdown, getup, techs: `BUSY`.
23. Specials (any): `BUSY` for their whole duration.

### Shared state

24. Damage, stocks and kill credit are shared: take damage as one character,
    switch, the meter is the same; KO the opponent with a projectile fired
    before switching: the KO counts for you.
25. Double jump as Fox, switch to Kirby midair: Kirby has no midair jumps.
    Switch to Kirby before double jumping: Kirby has all five.
26. Mario cape stall, switch to Marth midair: dancing blade gives no
    height. Land: everything is back.
27. Respawn invincibility and ledge intangibility carry over with the same
    remaining time (blinking continues, not restarted).
28. Holding a Link bomb or Peach turnip when switching: the incoming
    character holds it; no duplicate appears.
29. Switch right after being thrown off the ledge with a regrab cooldown:
    the cooldown still applies.

### Per-slot state

30. Charge Samus' shot, switch away and back later: same charge, and it did
    not charge meanwhile. Same for DK punch, Mewtwo shadow ball, Sheik
    needles.
31. Use Bowser's fire breath until it shrinks, switch away for a while,
    come back: still short (no refill while out).
32. As Zelda, transform to Sheik, switch away and back: Sheik comes back.
    Down-B still transforms.
33. Kirby: copy an ability, switch away and back: still has it, including a
    charged copy (Samus shot). Taunt: loses it; switch away and back: still
    gone.
34. Ice Climbers: get Nana to 50%, switch away and back: Nana returns next
    to Popo at 50%. Let Nana die, switch away and back: Popo alone until
    you lose a stock; then Nana returns at 0%.
35. Switching out of Ice Climbers is `BUSY` while Nana is in hitstun,
    knocked down, helpless or grabbing.
36. Lose a stock with Kirby holding an ability on another slot: after
    respawning, that Kirby has no ability; charged Samus on another slot is
    empty. G&W's bucket on another slot keeps its fill.

### Entities

37. Fire Fox lasers, Samus missiles, Link arrows and bombs, Peach turnips,
    Mewtwo shadow ball, then switch while they fly: they keep flying and
    hit; no character reappears; no projectile freezes or lingers.
38. Throw Link's boomerang and switch: it disappears as on Link's KO; later
    Link can throw again.
39. Switch while Nana holds an item: the item disappears with her (no
    floating item).
40. No stuck sounds after switching out of a sound loop (Bowser fire,
    Samus charge).

### Deaths, matches, memory

41. Get KOed (all four blast zones, star KO): respawn as the character
    that died; switching works again once off the platform.
42. Last stock lost: the match ends normally, results screen shows.
    Rematch several times; rosters work every match.
43. Time match with KOs: other slots reset after each KO.
44. Four players, all with four-character rosters on a large stage: some
    slots show `-`; nothing crashes during load or play; switching to
    every available slot works.
45. Rosters with Zelda/Sheik and Ice Climbers in every slot of two players
    (eight two-fighter characters): no crash, switches keep working
    (fighters are recreated on demand).
46. First switch to each slot happens on the press frame (frame advance in
    Dolphin: the pressed frame already shows the incoming character).

### Slippi online

47. Two clients with the same patched ISO and settings, each with a roster:
    first game shows `TAG SYNC` then the HUD stays empty and D-pad switching
    does nothing; second game against the same opponent: tag works for both.
48. Play a long game with many switches at high ping (rollbacks): no
    desync warning from Slippi.
49. One client patched with `--cooldown 45`: tag stays off for both.
50. One client changes its roster between games: that game is off, the
    next one works.
51. Replays: known not to work for tag matches (documented).
