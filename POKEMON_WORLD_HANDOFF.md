# Pokémon World: project handoff

Repo: https://github.com/akiaschroeder-web/Pokemon-World (branch `master`). Base: pokeemerald-expansion, with FireRed (pokefirered) content brought in for Kanto.

## What the game is

Pokémon World is a fan game that puts the main-series regions into one game you can play from start to finish. It is meant to feel like an official Gen 3 game, with every region's own story, gyms, trainers, wild Pokémon and music intact. You pick where to begin and can then travel between regions. It's a side project spun off from the owner's custom-region hack ("Sedgwick County", a separate repo: akiaschroeder-web/sedgwick-pokemon) and shares some of its assets and ideas. It is simpler than that project: mostly the existing regions, put together.

### Regions and sources
| Region | Source | Status |
|---|---|---|
| Hoenn | pokeemerald-expansion (Emerald) | In the game |
| Sinnoh | LiderMorti00's Sinnoh port on pokeemerald-expansion | In the game (flag split still partly to do) |
| Kanto | FireRed (pokefirered), merged in | Maps, scripts, trainers and encounters in; groundwork done |
| Johto | The Heart & Soul pokeemerald-expansion project | Planned, next big region |
| Unova | Probably built mostly by the owner | Future |
| Kalos, Galar | To be looked at | Future |
| Alola | The owner is worried about it | Maybe |
| Orre (Colosseum/XD) | Looks tough | Maybe; Shadow Pokémon might come later, but first decide how Shadow Lugia would affect Johto's story |

The Heart & Soul and Sinnoh developers gave permission to use their work as long as they're credited. **Credits are required:** keep PROJECT_CREDITS.md and in-game credits up to date. That includes pokeemerald-multiplatform (MIT), which the web port draws on.

### How it plays (design decisions already made)
- **Character select before anything else.** Before the professor's speech, the player chooses:
  1. the **starting region**;
  2. their **player character**, from any region's player designs (Brendan, May, Lucas, Dawn and so on), not just the region they start in, and carried through the whole game;
  3. their **rival's design and gender**. The rival can't be the same design as the player.
  - Stretch goal: let the player be any normal rival character (e.g. Barry) and the other way round.
  - Hoenn's rival uses the chosen rival and their name.
- **Only one Kanto, and it changes over time.** FireRed's Kanto comes first, then switches to its "3 years later" state (HGSS-era) after Johto's story.
- **Kanto's badges stay the same whatever order you play in.** Whether Kanto or Johto is done first, there are no redone gyms for Mt. Silver.
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
   - the Johto port from Heart & Soul
   - the Kanto time change
   - the rest of the Sinnoh flag split
   - a Town Map per region
   - the Badge Case
   - travel between regions
   - region-based battle music

## Region IDs (from the Kanto groundwork)
- **Flags:** region flags are 0x1000–0x1EFF (Kanto from 0x1000, Johto from 0x1400). Region trainer flags start at 0x1900 (`REGION_TRAINER_FLAGS_START`).
- **Vars:** region vars are 0x4800–0x49EF.
- **Trainers:** Kanto trainer IDs are 1530–2152. `GetTrainerFlagId()` maps trainers to flags.
- **Save data:** SaveBlock3 holds `regionFlags[480]` and `regionVars[496]`.
- **Generator:** the ID headers are generated by `tools/pokemon_world/gen_kanto_ids.py`.
- `debug.c` has a WarpKanto menu for testing.
