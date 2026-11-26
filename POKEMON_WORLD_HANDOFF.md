# Pokémon World: project handoff

Repo: https://github.com/akiaschroeder-web/Pokemon-World (branch `master`). Base: pokeemerald-expansion, with FireRed (pokefirered) content brought in for Kanto.

## What the game is

Pokémon World is a fan game that puts the main-series regions into one game you can play from start to finish. It is meant to feel like an official Gen 3 game, with every region's own story, gyms, trainers, wild Pokémon and music intact. You pick where to begin and can then travel between regions. It's a side project spun off from the owner's custom-region hack ("Sedgwick County", a separate repo: akiaschroeder-web/sedgwick-pokemon) and shares some of its assets and ideas. It is simpler than that project: mostly the existing regions, put together.

### Regions and sources
| Region | Source | Status |
|---|---|---|
| Hoenn | pokeemerald-expansion (Emerald) | In the game |
| Sinnoh | LiderMorti00's Sinnoh port on pokeemerald-expansion | In the game (flag split still partly to do) |
| Kanto | FireRed (pokefirered), merged in; after the time skip, Heart & Soul's Gold/Silver-era Kanto | In the game: FireRed's Kanto first, then the Gold/Silver-era one after Johto's Champion |
| Johto | The Heart & Soul pokeemerald-expansion project | In the game (maps, story, trainers, wild Pokémon, HGSS music); can be the starting region |
| Unova | Pokémon Black (the owner's ROM), drawn in 3D | All zones, BW's scripts, trainers, items, music; startable; see “Unova” below |
| Kalos, Galar | To be looked at | Future |
| Alola | The owner is worried about it | Maybe |
| Orre (Colosseum/XD) | Looks tough | Maybe; Shadow Pokémon might come later, but first decide how Shadow Lugia would affect Johto's story |

The Heart & Soul and Sinnoh developers gave permission to use their work as long as they're credited. **Credits are required:** keep PROJECT_CREDITS.md and in-game credits up to date. That includes pokeemerald-multiplatform (MIT), which the web port draws on.

### How it plays (design decisions already made)
- **One engine everywhere.** pokeemerald-expansion's species, moves, items, battle engine and overworld systems (e.g. following Pokémon, Poké Balls, Berries) are used in every region. Where expansion already has a feature or item, imported content is pointed at expansion's rather than porting a second copy.
- **Character select before anything else.** Before the professor's speech, the player chooses (`src/main_menu.c`, `Task_PwSetup_*`; designs in `src/pokemon_world.c`, `gPwDesigns`):
  1. the **starting region**: Hoenn (Birch's speech, then the truck), Sinnoh (Rowan's intro, Twinleaf) or Johto (Heart & Soul's Prof. Oak speech on Birch's stage, then the player's room in New Bark Town). `WarpToTruck()` in `src/new_game.c` picks the start map;
  2. their **player character**, from any region's player designs (Brendan, May, Lucas, Dawn, Ethan, Kris), not just the region they start in, and carried through the whole game;
  3. their **rival's design**: any design but the player's own (Barry is rival-only for now).
  - Stretch goal: let the player be any normal rival character (e.g. Barry) and the other way round.
  - Hoenn's (and Sinnoh's) rival uses the chosen rival and their name.
  - **Johto's rival is Silver**, plays his own story, and is named by the player in Elm's lab (`{JOHTO_RIVAL}`, `SaveBlock3.johtoRivalName`); the rival chosen at the start stays the Hoenn/Sinnoh rival.
- **Only one Kanto, and it changes over time.** FireRed's Kanto comes first, then switches to its "3 years later" state (Heart & Soul's Gold/Silver-era maps) when the player first becomes **Johto's Champion** (`FLAG_PW_KANTO_TIME_SKIP`, `src/kanto_time_skip.c`): from then on every warp into FireRed's mainland Kanto lands in the matching later-Kanto map.
- **Kanto's badges stay the same whatever order you play in.** Whether Kanto or Johto is done first, there are no redone gyms for Mt. Silver: a later-Kanto gym leader gives the Kanto badge only if the player doesn't have it yet; otherwise the battle is a rematch (no badge, fanfare or TM).
- **The Sevii Islands stay reachable** after the time skip (the Seagallop from the later Vermilion port), and are planned to become the **hub between regions**.
- **Badge Case** key item that shows the badges collected in every region, with badge artwork.
- **Region-specific items:** items that would clash stay separate per region, e.g. a Hoenn Town Map, a Sinnoh Town Map and a Kanto Town Map, each showing its own region.
- **Battles and wild encounters** run on pokeemerald-expansion's battle engine everywhere. Battle music follows the current region and the kind of battle or encounter.
- **Region data:** each region keeps its own story flags, vars and trainer flags in separate ranges (see Region IDs below), so stories don't step on each other.

### Platform
- The owner no longer cares about it being a real GBA ROM: it has outgrown the 32 MB limit. It's now **its own program**: a PC build, and for iPhone a **web app version of the PC build** played in Safari. No native iPhone app is needed.
- The web build is published on GitHub Pages from `master`.
- The owner mostly tests on a phone, through the web build.
- Only build at major milestones; builds are slow, so don't trigger one for every small change.

## How the web build works
- `make PLATFORM=web web` (see `web.mk`). Output goes to `build/web/site/`. Needs emsdk 3.1.74, host `cpp`/`as` (binutils), python3, libpng-dev.
- `PORTABLE` define: the GBA hardware (IO regs, palette, VRAM, OAM, flash) becomes arrays in `src/platform/bios.c`. DMA goes through `PlatformDmaSet` (`src/platform/dma.c`). The renderer is `src/platform/gba_fast_draw.c`. Browser glue is `src/platform/web.c`. Page side: `platform/web/shell.html`, `platform/web/pokeworld.js`, with touch buttons, keyboard, localStorage saves and save export/import that also takes emulator .sav files.
- Data `.s` files are assembled with host `as --32` and converted by `tools/web/elf2wasm.py`. Function signatures come from `tools/web/wasm_functypes.py`, and cross-file constants from `--abs data_constants.txt`.
- Asyncify: `VBlankIntrWait` draws the frame, runs the V-blank interrupt, then yields to the browser.
- Sound: `src/m4a_1.s` is replaced by C. `src/platform/m4a_engine.c` is the sequencer and `SoundMain`; `sound_mixer.c` mixes the PCM channels (envelopes, reverb, reversed and compressed cries) in float at 42048 Hz, 704 samples per frame; `cgb_audio.c` plays the Game Boy channels from the `REG_NRxx` registers that `CgbSound` (in `src/m4a.c`, unchanged) writes. `VBlankIntrWait` hands each frame's samples to `Module.pwQueueAudio` in `platform/web/pokeworld.js`, which queues about 70 ms and plays it through an AudioWorklet (or a ScriptProcessorNode). The AudioContext starts on the first touch or key press. In turbo mode sound is dropped.
  - Testing: `AUDIOLOG=1` makes the page log loudness, active channels and cries once a second; the drive.js step `["audio", "name", frames]` writes `name.wav`.
- CI: `.github/workflows/build-web.yml` runs when the commit message contains `[web]` or from manual dispatch. It builds (with a build cache), runs a smoke test, publishes to Pages, and force-pushes screenshots, `build-errors.txt` and the built site to the `web-screenshots` branch.
- `.github/workflows/build-rom.yml` still builds the GBA ROM and reports its size.

## Debugging tools (tools/web/)
- `drive.js`: plays the game in headless Chromium using steps such as until, press, pressUntil, hold, wait, shot, vram, call, trace and state.
  - Use `TURBO=1` to run as fast as possible with exact-frame key presses.
  - `["call","Web_SetTrace",1]` prints every task, callback and sprite callback before it runs, with names.
  - `["call","Web_DebugWindows"]` dumps windows, BG tilemap buffers and heap blocks.
  - `DEBUG=pw:browser*` shows Chromium's crash output.
  - It needs a table built with `python3 tools/web/wasm_where.py index.wasm --table --json > table.json`.
- `wasm_where.py`: maps a crash offset or function pointer to a name. The build links with `--profiling-funcs`.
- `render_bg.py`: draws a BG from a `vram` dump.
- `smoke_test.js`: the CI check.

## In-game debug menu (R+Start; keyboard S+Enter in the web build)
- **Warp to town…** → Kanto… (FireRed's), Kanto (3 yrs later)… (Heart & Soul's Gold/Silver-era towns), Johto…, Hoenn…, Sinnoh… (`src/debug.c`, `sDebugMenu_Actions_WarpRegion`). With the time skip on, a warp into FireRed's Kanto lands in the later one anyway.
- **Flags & Vars… → Toggle Kanto time skip**: on does what becoming Johto's Champion does (`Debug_EventScript_PwKantoTimeSkip`, which also runs Heart & Soul's later-Kanto set-up); it applies from the next warp. Off goes back to FireRed's Kanto.

## Johto and the later Kanto: the Heart & Soul import
Heart & Soul (a pokeemerald-expansion project; read-only copy at `~/ref/pokemonHnS`, never built or run) is converted by generators. **Generated files are changed in the generator, never by hand**, so the import stays re-runnable.
- `tools/pokemon_world/import_hns.py` (its docstring lists every file it writes): maps, layouts, tilesets, door animations, map sections, heal locations, Johto's flags/vars/trainer IDs (in Johto's region ranges), map scripts and the shared scripts they need, trainers and trainer classes/pics, wild encounters, multichoice menus, Heart & Soul's new-game set-up (`data/scripts/hns_new_game.inc`), and the Ethan/Kris/Prof. Oak pictures. Everything from Heart & Soul gets `_Hns`/`_HNS` names. Our own changes to its scripts are `pw_script_patches()` near the end (time skip at the Hall of Fame, later-Kanto set-up moved off the S.S. Aqua, gym rematches, Seagallop at Vermilion, Kurt's berry-to-ball bag check). Tables it uses are near the end too (`SHARED_FLAGS`, `HNS_SPECIALS`, `CLASS_MAP`, `MUSIC_STANDIN`, ...).
- `tools/pokemon_world/hns_sprites.py` (overworld sprites we lacked) and `hns_music.py` (HGSS songs, voicegroups, samples) are called by the importer.
- `tools/pokemon_world/gen_kanto_time_skip.py`: the FireRed → later-Kanto map table; re-run it after the importer.
- `tools/pokemon_world/gen_kanto_ids.py`: Kanto's (FireRed's) region ID headers.
- Hand-written companions: `asm/macros/hns_compat.inc` (Heart & Soul's script commands), `src/hns_specials.c`, `src/hns_bug_contest.c`, `src/hns_sliding_puzzle.c`, `src/data/tilesets/tileset_anims_hns.h`, `src/kanto_time_skip.c`, `data/scripts/kanto_time_skip.inc`.
- **Re-running:** from the repo root, `python3 tools/pokemon_world/import_hns.py` (optional `--hns <path>`; needs a host gcc to read our constants), then `python3 tools/pokemon_world/gen_kanto_time_skip.py`. It takes out what it added before (`tools/pokemon_world/hns_import_state.json`) and prints a report of what was stubbed or mapped at the end. Then build.
- Every new game runs Johto's starting flags (`EventScript_ResetAllMapFlags_Hns`) next to Hoenn's/Sinnoh's and Kanto's, whatever the starting region; a Johto start also gets Heart & Soul's starting items (`EventScript_NewGameItems_Hns`).
- Kurt (Azalea) makes Poké Balls from Berries, as in Heart & Soul: 5 Pecha → Love, 5 Rawst → Lure, 5 Cheri → Friend, 5 Oran → Heavy, 5 Chesto → Moon, 5 Aspear → Fast, 5 Persim → Level Ball, straight away (no daily wait). These are expansion's balls and Berries.
- **Following Pokémon** are expansion's (`OW_FOLLOWERS_ENABLED` in `include/config/overworld.h`, on in every region). Heart & Soul uses the same system, so its scripts' `UpdateFollowingPokemon` / follower field moves are expansion's own. Species without an overworld sprite show expansion's placeholder (`OW_SUBSTITUTE_PLACEHOLDER`).

## Unova: Pokémon Black's real 3D maps
Unova isn't drawn by the GBA layers. The page draws Pokémon Black's own map models with WebGL
(`platform/web/unova3d.js`), using BW's field camera, and the game's picture goes on top with its empty parts
transparent. Our engine still runs everything: walking, collision, doors, scripts, battles, and the player
sprite chosen at setup.
- **Converter:** `PYTHONPATH=~/pylib python3 tools/pokemon_world/import_bw.py "<Pokemon Black .nds>"` (needs ndspy:
  `pip install --target ~/pylib ndspy`). It writes `graphics/unova/` (meshes `.pwm` + `.json` with cameras,
  textures for all four seasons, and tile heights), a GBA map per zone (`data/maps/Unova_*`, the
  `gMapGroup_Unova` group, layouts on the invisible `gTileset_Unova3D` tilesets that only carry behaviours),
  the warps, `src/data/unova_zones.h` and the `MAPSEC_NUVEMA_TOWN` place name. NITRO model/texture reader:
  `tools/pokemon_world/bw/nitro.py`.
- **Game side:** `src/platform/web3d.c`. Map sprites (the ones with `coordOffsetEnabled`) leave the 2D picture
  and go to the page with their pixels and positions, and stand in the 3D scene as upright billboards.
  Hooks: `sprite.c` (BuildOamBuffer, LoadOam), `overworld.c` (camera start), `gba_fast_draw.c` (skip those
  sprites; empty backdrop keeps bit 15 clear), `web.c` (transparent pixels). Palette fades are applied to
  the 3D picture too. Seasons follow the real month like BW (January = spring).
- **Converted so far:** all of Unova's 426 zones as 450 maps (zones that aren't rectangles are split; the
  pieces are joined with map connections, and the 3D view draws the neighbouring chunks, so walking is
  seamless), with BW's doors and stairs, wild Pokémon (grass, surfing, fishing; one table per season,
  picked by the month in `src/wild_encounter.c`), BW's people with their own sprites
  (`bw/sprites.py` → `graphics/object_events/pics/unova`, `OBJ_EVENT_GFX_UNOVA_OBJ<n>`) and signs, with
  what they say taken from BW's scripts and text (`bw/scripts.py`). Debug menu → Warp to town →
  Unova… → Nuvema Town.
- **BW's own scripts run** (`src/bw_script.c`, a Gen 5 script interpreter; data from `bw/vmdata.py`): NPCs,
  signs, step triggers and map load/frame scripts call `bwscript <zone>, <id>`. Messages, yes/no, movement,
  flags/variables (BW flag n = `FLAG_UNOVA_START + n`, work var 0x4000+n = `VAR_UNOVA_START + n`, in
  SaveBlock1), items, gifts, wild and trainer battles (BW trainer n = `UNOVA_TRAINERS_START + n`), the
  starter gift box (our starter screen: Snivy/Tepig/Oshawott), badges (`FLAG_UNOVA_BADGE01_GET`…), shops
  (BW's lists, the counter's by badge count), BGM commands, warps. Unknown commands are skipped by size.
  Characters BW shows later start hidden (`src/data/bw_start_flags.h`, set on first entry).
- **Trainers** (`bw/trainers.py`): all 615 into `src/data/trainers_unova.party` with BW's pictures
  (`TRAINER_PIC_BW_*`, 80×80 scaled to 64×64), classes (ours when we have it, else `TRAINER_CLASS_*_BW`),
  and their lines (a/0/0/2 file 189). Trainer NPCs (BW scripts 3000+) get our `trainerbattle`; their flags
  are `FLAG_UNOVA_TRAINERS_START + n` (`GetTrainerFlagId`).
- **Items:** item balls (scripts 7000+ → `finditem`), hidden items (8000+; BW's flags 900+ through
  `UNOVA_HIDDEN_ITEM_ID_BASE` / `HIDDEN_ITEM_FLAG()`), and BW's key items we lacked (`bw/items.py`: Light/Dark
  Stone, Gram 1–3, Liberty Pass, Pass Orb, Prop Case, Dragon Skull, Xtransceiver, with BW's icons).
- **Music** (`bw/music.py`): 90 of BW's songs (zones, battles, victories, fanfares) as `MUS_BW_*`: each SSEQ
  becomes a MIDI file for mid2agb (all tracks on; loops marked), its bank a voicegroup, its samples .wav files
  (8-bit, exact pitch in an `agbp` chunk). Routes change with the season (`BwSeasonMusic`), battles use
  BW's themes in Unova (`MUSIC_STYLE_UNOVA`), BGM players have 16 tracks now. Two songs ndspy can't read
  (`SEQ_BGM_H_01`, `SEQ_BGM_D_06`, command 0x82) keep a placeholder.
- **Start:** "UNOVA" at the new-game region choice: the player's room in Nuvema Town (the intro speech is still Birch's).
- **Cutscenes:** BW's event camera (EVCamera*) moves the 3D view (scene block words 11-19, eased in
  `unova3d.js`); cutscene characters (ActorNew, `sBwObjGfx`), teleports (ActorSetGPos), list menus, reserved
  follow-up scripts, the other warp kinds, money, party queries/selection and clock commands are handled.
  Set `BW_TRACE_ON` to 1 in `src/bw_script.c` to log scripts and skipped commands to the browser console.
  Checked: the opening (room scene with the camera pan, gift box -> starter screen -> Bianca's battle).
- **Trainer eye music:** a Unova trainer's `encounterMusic` is the index of BW's eye theme (from BW's class
  table), played by `GetUnovaTrainerEyeMusic`.
- **Not yet:** sound effects in BW scripts (SEPlay), Call3DDemo cutscenes (e.g. N's castle), building/door and
  map texture animations, rail NPCs (40, on Castelia's curved streets etc.; needs BW's rail geometry), the
  Badge Case's Unova row and a Unova Town Map (BW's art not located yet), Juniper's intro, two songs ndspy
  misreads (`SEQ_BGM_H_01`, `SEQ_BGM_D_06`). Note: the map group count is at its limit (128).
- BW zone ids, area/texture sets and formats: see the converter's comments.

## Known gaps / not yet played (Johto stages 1–7)
- ~~Wild battles turning into doubles~~ (fixed): a web-only compiler problem. Expansion's `if_comptime` (`include/metaprogram.h`) relies on a GCC trick that clang (Emscripten) folds the other way, so in the browser every `RandomPercentage()` was a coin flip (the 0% double-battle chance gave doubles half the time; battle effect chances too). It now has a clang version. The GBA build (GCC) was never affected.
- ~~The GS Ball can't be thrown~~ (fixed): it's a Poké Ball again, as in Heart & Soul (`BALL_GS`, a sure catch, Heart & Soul's ball sprite), in the Poké Balls pocket and still Kurt's Celebi item (can't be sold or tossed). A follower caught in it comes out of an ordinary Poké Ball (no overworld GS Ball sprite).
- ~~Tutor menus~~ (fixed): the Trainer Hill courtyard's third Frontier tutor (Frenzy Plant / Blast Burn / Hydro Cannon) has its own list and descriptions (`SCROLL_MULTI_BF_MOVE_TUTOR_3`), and the Ilex Forest tutor teaches Headbutt (it was set to no move).
- ~~Music stand-ins~~ (fixed): Platinum's Fight Area theme and Ruby/Sapphire's heavy-rain theme now come over with their own Heart & Soul instruments (`hns_music.py` OTHER_SETS, `sound/voicegroups/hns_other.inc`). The heavy-rain theme is an assembly song (`sound/songs/*.s`, new `SONG_OBJS` in Makefile/web.mk).
- ~~Garbled names in battle~~ (fixed): web-only. `src/text.c` shifted 32-bit values by 32 (undefined in C; the GBA gives 0, WebAssembly doesn't), which drew letters twice (ghost letters, doubled gender sign) and erased 8-pixel-wide ones ("Lv"). A sanitizer build (`-fsanitize=shift-exponent`) found no other bad shifts through a new game, save/load, a Johto start and a long battle.
- Town Map / Fly map per region (done, `src/region_map.c`): Hoenn's map is back; Sinnoh has the Sinnoh port's map, Johto Heart & Soul's Johto-only map, Kanto (both eras) and the Sevii Islands FireRed's maps. What's left / not checked:
  - Fly only goes to the places on the map shown (`sFlyLocations`), never to another region, and from the Sevii maps only to the islands on that map (One–Three, Four–Five, Six–Seven).
  - Sinnoh's towns set no visited flags in their scripts, so entering a Sinnoh fly place sets `FLAG_SINNOH_VISITED_*` (`SetSinnohVisitedFlag`, called from `src/overworld.c`). Saves from before this only get them on the next visit. Sinnoh's Pokémon League lands at the south Pokémon Center (so Fly can't skip Victory Road).
  - Kanto counts FireRed's `FLAG_WORLD_MAP_*` or Heart & Soul's `FLAG_VISITED_*`, and lands at FireRed's heal locations (the time skip moves them to the later Kanto). Route 28 (later Kanto only) sits west of Route 22 on FireRed's map; `MAPSEC_KANTO_ABANDONED_SHIP` (only Heart & Soul's unused test maps) has no place.
  - Heart & Soul's map sections have the coordinates of its combined Johto+Kanto map in `region_map_sections.json`; the region map reads their places from the layout of the map shown instead (`GetRegionMapSectionRect`). The S.S. Aqua shows at Olivine, Johto's Safari Zone at its gate. Fixing the json in `import_hns.py` would be cleaner.
  - Heart & Soul's Trainer Hill courtyard uses Hoenn's `MAPSEC_TRAINER_HILL`, so its Town Map shows Hoenn. The Sinnoh map also shows Hoenn's Battle Frontier section (named only, no Fly).
  - The PokéNav's city zoom maps are Hoenn's only. The player icon follows the chosen character (Brendan, May, Lucas, Dawn, Ethan, Kris).
  - Checked in the headless driver (new Hoenn game, debug warps + Toggle Fly Flags): the Fly map of Hoenn, Sinnoh, Kanto and Johto with the cursor and icon on the player's town, and a Fly from New Bark to Cherrygrove. Not checked: the Town Map item / wall maps, the Pokédex area screen outside Hoenn, the Sevii maps, Fly in the later Kanto.
- Sevii passes (done): after the time skip, a player who never started FireRed's Sevii story gets the Tri-Pass and Bill's Meteorite from Bill in Goldenrod (`EventScript_PwBillSeviiOffer`, data/scripts/kanto_time_skip.inc); FireRed's Sevii story then goes on from One Island as usual. Not played.
- The GBA ROM hasn't been built since the Johto import: WSL has no ARM toolchain yet (`sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi`).
- Not played through: the Johto story beyond New Bark, Kurt's balls, the Hall of Fame time skip, later-Kanto gym rematches, the Seagallop, Ethan/Kris on bikes/surfing/fishing and in battle, the credits roll (it's longer now; Emerald's credits music may end before it does).
- Heart & Soul's female player is **Kris** (Crystal), not Lyra: it has no Lyra sprites. Lyra would need new overworld and battle sprites.

## Status (Nov 22, 2026)
Working in the browser: boot, intro, title, main menu, the PW character select, Birch's speech with its text box, and the naming screen.

- **Fixed:** after character select, BG0's tilemap buffer was freed but BG0 still pointed at it, so the text box got drawn as tilemap. The fix is `UnsetBgTilemapBuffer(0)` after `FreeAllWindowBuffers` in `src/main_menu.c`. It's a real bug on hardware too.
- **Current bug:** the browser tab crashes (renderer SEGV) the moment OK is confirmed on the player naming screen (Hoenn path), while `Task_NamingScreen` is in its pressed-OK/fade-out states. No wasm exception is reported; the crash happens in V8 itself. Next step: run `drive.js` with `Web_SetTrace(1)` just before pressing OK to find the last callback, then narrow down.

## Next
1. Fix the naming-screen crash. Then reach the truck and Littleroot, test walking, and test saving and loading.
2. Test the Sinnoh start (Rowan's intro) the same way.
3. Sound: done (see above). Still to check by ear on an iPhone: levels, Game Boy channel accuracy, latency.
4. A PC (desktop) build of the same portable code.
5. Game work:
   - ~~the Johto port from Heart & Soul~~ (done, Johto stages 1–7; see the known gaps below)
   - ~~the Kanto time change~~ (done: at Johto's first Hall of Fame)
   - ~~region-based battle music~~ (done, Johto stage 4)
   - the rest of the Sinnoh flag split
   - ~~a Town Map per region~~ (done: each region's Town Map / Fly map; Fly stays within the region, see the known gaps)
   - Sinnoh's visited flags in its own map scripts, as part of the Sinnoh flag split (for now entering a place sets them)
   - the Badge Case
   - travel between regions (the Sevii Islands as the hub)

## Region IDs (from the Kanto groundwork)
- **Flags:** region flags are 0x1000–0x1EFF (Kanto from 0x1000, Johto from 0x1400). Region trainer flags start at 0x1900 (`REGION_TRAINER_FLAGS_START`).
- **Vars:** region vars are 0x4800–0x49EF.
- **Trainers:** Kanto trainer IDs are 1530–2152. `GetTrainerFlagId()` maps trainers to flags.
- **Save data:** SaveBlock3 holds `regionFlags[480]` and `regionVars[496]`.
- **Generator:** the ID headers are generated by `tools/pokemon_world/gen_kanto_ids.py`.
- `debug.c` has a WarpKanto menu for testing.
