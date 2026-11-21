# Sinnoh port status

Sinnoh comes from [LiderMorti00's Sinnoh project](https://github.com/LiderMorti00/sinnoh-pokeemerald-expansion) (used with permission, see `PROJECT_CREDITS.md`). That project replaced Hoenn rather than sitting beside it, so its content is being moved out of Hoenn's slots step by step.

## Done

- **Trainers.** Every Sinnoh trainer that lived in a Hoenn trainer slot now has its own `TRAINER_SN_*` ID, and Hoenn's original trainers are restored. `trainer_id_map.csv` lists each old slot, its new ID and the trainer's name. Sinnoh's readable aliases (`TRAINER_ROARK_1`, `TRAINER_BARRY_LAKE_*`, …) still work and point at the new IDs.
  - This raises `MAX_TRAINERS_COUNT_EMERALD` from 864 to 1536, which adds 84 bytes of trainer flags to the save. If the build fails a SaveBlock size check, that is the cause.

## Still shared with Hoenn (to do)

- **Story flags.** About 150 `FLAG_SN_*` flags are aliases of Hoenn flags (e.g. `FLAG_SN_LAKE_VALOR_EXPLODED` = `FLAG_RECEIVED_HM_CUT`), and Sinnoh scripts use about 140 Hoenn flags directly.
- **Trainer-flag aliases.** 74 `FLAG_SN_*` flags reuse trainers' "defeated" flags as general flags. They now point at Sinnoh trainers' flags rather than Hoenn's, but they still need flags of their own.
- **Badges.** Sinnoh gyms set Hoenn's `FLAG_BADGE0x_GET`.
- **Rematch table.** Sinnoh rewrote rows of `gRematchTable`, so Hoenn's VS Seeker / Match Call rematches don't work yet. The table is capped by `MAX_REMATCH_ENTRIES` in the save, so both regions' rematches need a plan.
- **New game start** is Twinleaf Town.
- **Encounter music:** Sinnoh's Galactic encounter theme needs its own music style (it used to replace Team Aqua's).
