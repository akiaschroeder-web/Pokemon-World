#include "global.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "pokemon_world.h"
#include "constants/event_objects.h"
#include "constants/vars.h"

// Pokémon World: starting region and character designs.

static const u8 sText_Brendan[] = _("BRENDAN");
static const u8 sText_May[] = _("MAY");
static const u8 sText_Lucas[] = _("LUCAS");
static const u8 sText_Dawn[] = _("DAWN");
static const u8 sText_Barry[] = _("BARRY");

const struct PwDesignInfo gPwDesigns[PW_DESIGN_COUNT] =
{
    [PW_DESIGN_BRENDAN] = { sText_Brendan, MALE,   TRUE,  TRAINER_PIC_BRENDAN },
    [PW_DESIGN_MAY]     = { sText_May,     FEMALE, TRUE,  TRAINER_PIC_MAY },
    [PW_DESIGN_LUCAS]   = { sText_Lucas,   MALE,   TRUE,  TRAINER_PIC_SINNOH_LUCAS },
    [PW_DESIGN_DAWN]    = { sText_Dawn,    FEMALE, TRUE,  TRAINER_PIC_SINNOH_DAWN },
    [PW_DESIGN_BARRY]   = { sText_Barry,   MALE,   FALSE, TRAINER_PIC_SINNOH_BARRY },
};

// ---------------------------------------------------------------- choices

static EWRAM_DATA struct
{
    u8 region;
    u8 player;
    u8 rival;
} sPending = {0};

void PW_SetPendingChoices(enum Region region, enum PwDesign player, enum PwDesign rival)
{
    sPending.region = region;
    sPending.player = player;
    sPending.rival = rival;
}

// Called at the end of NewGameInitData(), after the save (and its vars) were cleared.
void PW_ApplyPendingChoices(void)
{
    VarSet(VAR_PW_START_REGION, sPending.region);
    VarSet(VAR_PW_PLAYER_DESIGN, sPending.player);
    VarSet(VAR_PW_RIVAL_DESIGN, sPending.rival);
}

// The vars hold the choice once the game has started; before that (during the
// professor's intro) the pending choice is used.
static u32 GetChoice(u16 var, u8 pending)
{
    u32 value = VarGet(var);
    return value != 0 ? value : pending;
}

enum Region PW_GetStartRegion(void)
{
    u32 region = GetChoice(VAR_PW_START_REGION, sPending.region);
    return region != REGION_NONE ? region : REGION_HOENN;
}

enum PwDesign PW_GetPlayerDesign(void)
{
    u32 design = GetChoice(VAR_PW_PLAYER_DESIGN, sPending.player);
    return design < PW_DESIGN_COUNT && gPwDesigns[design].playable ? design : PW_DESIGN_NONE;
}

enum PwDesign PW_GetRivalDesign(void)
{
    u32 design = GetChoice(VAR_PW_RIVAL_DESIGN, sPending.rival);
    return design < PW_DESIGN_COUNT ? design : PW_DESIGN_NONE;
}

// ---------------------------------------------------------------- overworld sprites

enum
{
    PW_STATE_NORMAL,
    PW_STATE_MACH_BIKE,
    PW_STATE_ACRO_BIKE,
    PW_STATE_SURFING,
    PW_STATE_FIELD_MOVE,
    PW_STATE_UNDERWATER,
    PW_STATE_FISHING,
    PW_STATE_WATERING,
    PW_STATE_DECORATING,
    PW_STATE_COUNT
};

#define PW_GFX_SET(prefix)                                   \
    extern const struct ObjectEventGraphicsInfo prefix##Normal;     \
    extern const struct ObjectEventGraphicsInfo prefix##MachBike;   \
    extern const struct ObjectEventGraphicsInfo prefix##AcroBike;   \
    extern const struct ObjectEventGraphicsInfo prefix##Surfing;    \
    extern const struct ObjectEventGraphicsInfo prefix##FieldMove;

#define PW_GFX_SET_PLAYER_EXTRA(prefix)                      \
    extern const struct ObjectEventGraphicsInfo prefix##Underwater; \
    extern const struct ObjectEventGraphicsInfo prefix##Fishing;    \
    extern const struct ObjectEventGraphicsInfo prefix##Watering;   \
    extern const struct ObjectEventGraphicsInfo prefix##Decorating;

PW_GFX_SET(gObjectEventGraphicsInfo_Brendan)
PW_GFX_SET_PLAYER_EXTRA(gObjectEventGraphicsInfo_Brendan)
PW_GFX_SET(gObjectEventGraphicsInfo_May)
PW_GFX_SET_PLAYER_EXTRA(gObjectEventGraphicsInfo_May)
PW_GFX_SET(gObjectEventGraphicsInfo_PwLucas)
PW_GFX_SET_PLAYER_EXTRA(gObjectEventGraphicsInfo_PwLucas)
PW_GFX_SET(gObjectEventGraphicsInfo_PwDawn)
PW_GFX_SET_PLAYER_EXTRA(gObjectEventGraphicsInfo_PwDawn)
PW_GFX_SET(gObjectEventGraphicsInfo_RivalBrendan)
PW_GFX_SET(gObjectEventGraphicsInfo_RivalMay)
PW_GFX_SET(gObjectEventGraphicsInfo_RivalPwLucas)
PW_GFX_SET(gObjectEventGraphicsInfo_RivalPwDawn)
extern const struct ObjectEventGraphicsInfo gObjectEventGraphicsInfo_SinnohBarry;

#define PLAYER_SET(prefix)                               \
    {                                                    \
        [PW_STATE_NORMAL]     = &prefix##Normal,         \
        [PW_STATE_MACH_BIKE]  = &prefix##MachBike,       \
        [PW_STATE_ACRO_BIKE]  = &prefix##AcroBike,       \
        [PW_STATE_SURFING]    = &prefix##Surfing,        \
        [PW_STATE_FIELD_MOVE] = &prefix##FieldMove,      \
        [PW_STATE_UNDERWATER] = &prefix##Underwater,     \
        [PW_STATE_FISHING]    = &prefix##Fishing,        \
        [PW_STATE_WATERING]   = &prefix##Watering,       \
        [PW_STATE_DECORATING] = &prefix##Decorating,     \
    }

// The rival only ever walks, bikes, surfs or uses a field move.
#define RIVAL_SET(prefix)                                \
    {                                                    \
        [PW_STATE_NORMAL]     = &prefix##Normal,         \
        [PW_STATE_MACH_BIKE]  = &prefix##MachBike,       \
        [PW_STATE_ACRO_BIKE]  = &prefix##AcroBike,       \
        [PW_STATE_SURFING]    = &prefix##Surfing,        \
        [PW_STATE_FIELD_MOVE] = &prefix##FieldMove,      \
    }

#define SINGLE_SET(info)                                                         \
    {                                                                            \
        [PW_STATE_NORMAL] = &info, [PW_STATE_MACH_BIKE] = &info,                 \
        [PW_STATE_ACRO_BIKE] = &info, [PW_STATE_SURFING] = &info,                \
        [PW_STATE_FIELD_MOVE] = &info,                                           \
    }

static const struct ObjectEventGraphicsInfo *const sPlayerGfx[PW_DESIGN_COUNT][PW_STATE_COUNT] =
{
    [PW_DESIGN_BRENDAN] = PLAYER_SET(gObjectEventGraphicsInfo_Brendan),
    [PW_DESIGN_MAY]     = PLAYER_SET(gObjectEventGraphicsInfo_May),
    [PW_DESIGN_LUCAS]   = PLAYER_SET(gObjectEventGraphicsInfo_PwLucas),
    [PW_DESIGN_DAWN]    = PLAYER_SET(gObjectEventGraphicsInfo_PwDawn),
};

static const struct ObjectEventGraphicsInfo *const sRivalGfx[PW_DESIGN_COUNT][PW_STATE_COUNT] =
{
    [PW_DESIGN_BRENDAN] = RIVAL_SET(gObjectEventGraphicsInfo_RivalBrendan),
    [PW_DESIGN_MAY]     = RIVAL_SET(gObjectEventGraphicsInfo_RivalMay),
    [PW_DESIGN_LUCAS]   = RIVAL_SET(gObjectEventGraphicsInfo_RivalPwLucas),
    [PW_DESIGN_DAWN]    = RIVAL_SET(gObjectEventGraphicsInfo_RivalPwDawn),
    [PW_DESIGN_BARRY]   = SINGLE_SET(gObjectEventGraphicsInfo_SinnohBarry),
};

enum { ROLE_NONE, ROLE_PLAYER, ROLE_RIVAL };

static u32 GetRoleAndState(u16 graphicsId, u32 *state)
{
    switch (graphicsId)
    {
    // The player's own sprites (Brendan's for boys, May's for girls).
    case OBJ_EVENT_GFX_BRENDAN_NORMAL:     case OBJ_EVENT_GFX_MAY_NORMAL:     *state = PW_STATE_NORMAL;     return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_MACH_BIKE:  case OBJ_EVENT_GFX_MAY_MACH_BIKE:  *state = PW_STATE_MACH_BIKE;  return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_ACRO_BIKE:  case OBJ_EVENT_GFX_MAY_ACRO_BIKE:  *state = PW_STATE_ACRO_BIKE;  return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_SURFING:    case OBJ_EVENT_GFX_MAY_SURFING:    *state = PW_STATE_SURFING;    return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_FIELD_MOVE: case OBJ_EVENT_GFX_MAY_FIELD_MOVE: *state = PW_STATE_FIELD_MOVE; return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_UNDERWATER: case OBJ_EVENT_GFX_MAY_UNDERWATER: *state = PW_STATE_UNDERWATER; return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_FISHING:    case OBJ_EVENT_GFX_MAY_FISHING:    *state = PW_STATE_FISHING;    return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_WATERING:   case OBJ_EVENT_GFX_MAY_WATERING:   *state = PW_STATE_WATERING;   return ROLE_PLAYER;
    case OBJ_EVENT_GFX_BRENDAN_DECORATING: case OBJ_EVENT_GFX_MAY_DECORATING: *state = PW_STATE_DECORATING; return ROLE_PLAYER;
    // The rival: Hoenn's May/Brendan and Sinnoh's Barry.
    case OBJ_EVENT_GFX_RIVAL_BRENDAN_NORMAL:     case OBJ_EVENT_GFX_RIVAL_MAY_NORMAL:     *state = PW_STATE_NORMAL;     return ROLE_RIVAL;
    case OBJ_EVENT_GFX_RIVAL_BRENDAN_MACH_BIKE:  case OBJ_EVENT_GFX_RIVAL_MAY_MACH_BIKE:  *state = PW_STATE_MACH_BIKE;  return ROLE_RIVAL;
    case OBJ_EVENT_GFX_RIVAL_BRENDAN_ACRO_BIKE:  case OBJ_EVENT_GFX_RIVAL_MAY_ACRO_BIKE:  *state = PW_STATE_ACRO_BIKE;  return ROLE_RIVAL;
    case OBJ_EVENT_GFX_RIVAL_BRENDAN_SURFING:    case OBJ_EVENT_GFX_RIVAL_MAY_SURFING:    *state = PW_STATE_SURFING;    return ROLE_RIVAL;
    case OBJ_EVENT_GFX_RIVAL_BRENDAN_FIELD_MOVE: case OBJ_EVENT_GFX_RIVAL_MAY_FIELD_MOVE: *state = PW_STATE_FIELD_MOVE; return ROLE_RIVAL;
    case OBJ_EVENT_GFX_SINNOH_BARRY: *state = PW_STATE_NORMAL; return ROLE_RIVAL;
    default:
        return ROLE_NONE;
    }
}

const struct ObjectEventGraphicsInfo *PW_SubstituteGraphicsInfo(u16 graphicsId, const struct ObjectEventGraphicsInfo *info)
{
    u32 state = PW_STATE_NORMAL;
    const struct ObjectEventGraphicsInfo *swapped = NULL;

    switch (GetRoleAndState(graphicsId, &state))
    {
    case ROLE_PLAYER:
        swapped = sPlayerGfx[PW_GetPlayerDesign()][state];
        break;
    case ROLE_RIVAL:
        swapped = sRivalGfx[PW_GetRivalDesign()][state];
        break;
    }
    return swapped != NULL ? swapped : info;
}

// ---------------------------------------------------------------- trainer pictures

enum TrainerPicID PW_GetPlayerTrainerPic(enum TrainerPicID fallback)
{
    enum PwDesign design = PW_GetPlayerDesign();
    return design != PW_DESIGN_NONE ? gPwDesigns[design].trainerPic : fallback;
}

enum TrainerPicID PW_SubstituteOpponentTrainerPic(enum TrainerClassID trainerClass, enum TrainerPicID pic)
{
    enum PwDesign design = PW_GetRivalDesign();

    if (design == PW_DESIGN_NONE)
        return pic;
    if ((trainerClass == TRAINER_CLASS_RIVAL && (pic == TRAINER_PIC_BRENDAN || pic == TRAINER_PIC_MAY))
     || pic == TRAINER_PIC_SINNOH_BARRY)
        return gPwDesigns[design].trainerPic;
    return pic;
}
