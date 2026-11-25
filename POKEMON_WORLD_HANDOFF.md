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
| Unova | Probably built mostly by the owner | Future |
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
- Sound is silent stubs for now (`src/platform/gba_asm_stubs.c`).
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
3. Sound: port pokeemerald-multiplatform's music_player.c, sound_mixer.c and cgb_audio.c (MIT, credited), adapted to expansion's m4a structs and compressed cries. Output through Web Audio.
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
