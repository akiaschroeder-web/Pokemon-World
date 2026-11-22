# Credits

This project builds on the work of many people in the Pokémon ROM hacking community. Assets and code from the projects below are used with permission and credited as required.

## Engine & Codebase

- **pokeemerald-expansion** by **RHH (Rom Hacking Hideout)** and all of its [contributors](CREDITS.md)
  Based off RHH's pokeemerald-expansion 1.17.2 (master @ e037034) https://github.com/rh-hideout/pokeemerald-expansion/
- **pokeemerald** and **pokefirered** decompilations by **pret** — https://github.com/pret

## Regions

### Kanto & Sevii Islands
- Ported from **pokefirered** by **pret** — https://github.com/pret/pokefirered

### Johto — from Pokémon Heart & Soul
Used with permission of the Heart & Soul team — https://github.com/PokemonHnS-Development/pokemonHnS

- Lead Developer / Game Director: **Lil Dill**
- Developers: **TixoRebel**, **InfiniteBacon42**, **Exclsior**, **Phantonomy**, **DaniRainbow**

Also used: Johto badge artwork (Badge Case).

Brought over from Heart & Soul so far (converted by `tools/pokemon_world/import_hns.py`, files marked `_Hns`/`_hns`):
- Johto and Gold/Silver-era ("3 years later") Kanto maps: 418 maps with their layouts, map scripts and texts, events, connections and warps
- 98 tilesets (Johto/Kanto primaries and secondaries) with their palettes, metatiles and animation frames, and the Johto general tileset animation
- Door animations, region map sections and heal locations for Johto and later Kanto
- Trainer parties of the trainers on those maps (classes/pics Heart & Soul made itself are not brought over yet)
- Shared scripts and texts the maps use (Bug-Catching Contest, Battle Frontier Lounge nurse, and others)

Assets Heart & Soul credits in turn:
- Engine base: **Modern Emerald — Resetes12**
- Sprites: **Cesare_CBass**, **AveonTrainer**, **PurpleZaffre**, **BatimaTheBat**
- Tilesets: **Crystal Advance — Kertra**, **Ekat99**, **TheDeadHeroAlistair**, **Johto Redrawn Team**
- Maps / mapping references: **Crystal Advance — Kertra**, **Fire Gold — blackfragrant**, **SkidMarc25**
- Gameplay: **Crystal Legacy — TSP (TheSmithPlays)**
- Surfing Pokémon overworlds: **slawter666**, **wally-217**

### Sinnoh — from Sinnoh pokeemerald-expansion
Used with permission of **LiderMorti00 (Morti)** — https://github.com/LiderMorti00/sinnoh-pokeemerald-expansion

### Web/PC version — from pokeemerald-multiplatform
MIT License — https://github.com/gradenGnostic/pokeemerald-multiplatform

The hardware stand-ins in `src/platform/` (BIOS functions, software DMA, the GBA picture
renderer) come from this port, adapted for pokeemerald-expansion. They build on earlier
portable work by the pret community (the BIOS code is based on VBA-M).

## Original Work
- <YOUR TEAM HERE>
