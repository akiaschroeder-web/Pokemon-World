#ifndef GUARD_BATTLE_ENVIRONMENT_H
#define GUARD_BATTLE_ENVIRONMENT_H

#include "constants/battle.h"
#include "battle_bg.h"
#include "task.h"

struct BattleEnvironment
{
    u8 name[26];
    u16 naturePower;
    const void *secretPowerAnimation;
    u16 secretPowerEffect;
    enum Type camouflageType;
    u16 camouflageBlend;
    struct BattleBackgroundEntry entry;
    struct BattleBackground background;
    const void *palette;
    TaskFunc battleIntroSlide;
};

extern const struct BattleEnvironment gBattleEnvironmentInfo[BATTLE_ENVIRONMENT_COUNT];

// Pokémon World: region-specific battle background graphics.
struct BattleEnvironmentGfx
{
    struct BattleBackgroundEntry entry;
    struct BattleBackground background;
    const void *palette;
    const void *paletteEvening; // optional: used outdoors in the evening (Platinum-style)
    const void *paletteNight;   // optional: used outdoors at night
};

extern const struct BattleEnvironmentGfx gBattleEnvironmentGfxSinnoh[BATTLE_ENVIRONMENT_COUNT];

const struct BattleBackgroundEntry *GetBattleEnvironmentEntryGfx(u32 environment);
const struct BattleBackground *GetBattleEnvironmentBackgroundGfx(u32 environment);
u32 GetBattleBgFadePalettes(void);

#endif // GUARD_BATTLE_ENVIRONMENT_H
