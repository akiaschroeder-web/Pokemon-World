#ifndef GUARD_POKEMON_WORLD_H
#define GUARD_POKEMON_WORLD_H

// Pokémon World: the starting region and the player/rival character designs
// chosen before a new game begins.

#include "constants/regions.h"
#include "constants/trainers.h"

enum PwDesign
{
    PW_DESIGN_NONE,     // nothing chosen (old saves): fall back to the player's gender
    PW_DESIGN_BRENDAN,
    PW_DESIGN_MAY,
    PW_DESIGN_LUCAS,
    PW_DESIGN_DAWN,
    PW_DESIGN_BARRY,    // rival only, until Barry has a full set of player sprites
    PW_DESIGN_COUNT
};

struct PwDesignInfo
{
    const u8 *name;
    u8 gender;
    bool8 playable;
    enum TrainerPicID trainerPic; // front pic, and back pic in battle when playable
};

extern const struct PwDesignInfo gPwDesigns[PW_DESIGN_COUNT];

// New game setup: remembered until NewGameInitData() clears the save, then stored.
void PW_SetPendingChoices(enum Region region, enum PwDesign player, enum PwDesign rival);
void PW_ApplyPendingChoices(void);

enum Region PW_GetStartRegion(void);
enum PwDesign PW_GetPlayerDesign(void);
enum PwDesign PW_GetRivalDesign(void);

// Character swaps, applied where the game looks up sprites and trainer pictures.
struct ObjectEventGraphicsInfo;
const struct ObjectEventGraphicsInfo *PW_SubstituteGraphicsInfo(u16 graphicsId, const struct ObjectEventGraphicsInfo *info);
enum TrainerPicID PW_GetPlayerTrainerPic(enum TrainerPicID fallback);
enum TrainerPicID PW_SubstituteOpponentTrainerPic(enum TrainerClassID trainerClass, enum TrainerPicID pic);

// Badges: each region keeps its own 8.
#define PW_BADGES_PER_REGION 8
bool32 PW_HasBadge(enum Region region, u32 badgeIndex);
u32 PW_CountBadges(enum Region region);
u32 PW_GetObedienceBadgeCount(void);
bool32 PW_HasFieldMoveBadge(u32 fieldMove);

#endif // GUARD_POKEMON_WORLD_H
