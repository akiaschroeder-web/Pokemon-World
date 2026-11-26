#include "global.h"
#include "pokemon_world.h"
#include "badge_case.h"
#include "sinnoh_radar.h"
#include "item_use.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_stat_change.h"
#include "battle_pyramid.h"
#include "battle_pyramid_bag.h"
#include "berry.h"
#include "berry_powder.h"
#include "bike.h"
#include "coins.h"
#include "data.h"
#include "event_data.h"
#include "event_object_lock.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "fieldmap.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "fishing.h"
#include "fldeff.h"
#include "follower_npc.h"
#include "item.h"
#include "item_menu.h"
#include "item_use.h"
#include "mail.h"
#include "main.h"
#include "menu.h"
#include "menu_helpers.h"
#include "metatile_behavior.h"
#include "oras_dowse.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokeblock.h"
#include "pokemon.h"
#include "script.h"
#include "sound.h"
#include "strings.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "vs_seeker.h"
#include "constants/event_bg.h"
#include "constants/event_objects.h"
#include "constants/item_effects.h"
#include "constants/items.h"
#include "constants/songs.h"

static void SetUpItemUseCallback(u8);
static void FieldCB_UseItemOnField(void);
static void Task_CallItemUseOnFieldCallback(u8);
static void Task_PartyMenuItemUseFromField(u8);
static void Task_UseItemfinder(u8);
static void Task_CloseItemfinderMessage(u8);
static void Task_HiddenItemNearby(u8);
static void Task_StandingOnHiddenItem(u8);
static void PlayerFaceHiddenItem(enum Direction);
static void CheckForHiddenItemsInMapConnection(u8);
static void Task_OpenRegisteredPokeblockCase(u8);
static void Task_AccessPokemonBoxLink(u8);
static void ItemUseOnFieldCB_Bike(u8);
static void ItemUseOnFieldCB_Rod(u8);
static void ItemUseOnFieldCB_Itemfinder(u8);
static void ItemUseOnFieldCB_Berry(u8);
static void ItemUseOnFieldCB_WailmerPailBerry(u8);
static void ItemUseOnFieldCB_WailmerPailSudowoodo(u8);
static bool8 TryToWaterSudowoodo(void);
static void BootUpSoundTMHM(u8);
static void Task_ShowTMHMContainedMessage(u8);
static void UseTMHMYesNo(u8);
static void UseTMHM(u8);
static void Task_StartUseRepel(u8);
static void Task_StartUseLure(u8 taskId);
static void Task_UseRepel(u8);
static void Task_UseLure(u8 taskId);
static void Task_CloseCantUseKeyItemMessage(u8);
static void SetDistanceOfClosestHiddenItem(u8, s16, s16);
static void CB2_OpenPokeblockFromBag(void);
static void ItemUseOnFieldCB_Honey(u8 taskId);
static bool32 IsValidLocationForVsSeeker(void);

static const u8 sText_CantDismountBike[] = _("You can't dismount your BIKE here.{PAUSE_UNTIL_PRESS}");
static const u8 sText_ItemFinderNearby[] = _("Huh?\nThe ITEMFINDER's responding!\pThere's an item buried around here!{PAUSE_UNTIL_PRESS}");
static const u8 sText_ItemFinderOnTop[] = _("Oh!\nThe ITEMFINDER's shaking wildly!{PAUSE_UNTIL_PRESS}");
static const u8 sText_ItemFinderNothing[] = _("… … … …Nope!\nThere's no response.{PAUSE_UNTIL_PRESS}");
static const u8 sText_CoinCase[] = _("Your COINS:\n{STR_VAR_1}{PAUSE_UNTIL_PRESS}");
static const u8 sText_PowderQty[] = _("POWDER QTY: {STR_VAR_1}{PAUSE_UNTIL_PRESS}");
static const u8 sText_BootedUpTM[] = _("Booted up a TM.");
static const u8 sText_BootedUpHM[] = _("Booted up an HM.");
static const u8 sText_TMHMContainedVar1[] = _("It contained\n{STR_VAR_1}.\pTeach {STR_VAR_1}\nto a POKéMON?");
static const u8 sText_UsedVar2WildLured[] = _("{PLAYER} used the\n{STR_VAR_2}.\pWild POKéMON will be lured.{PAUSE_UNTIL_PRESS}");
static const u8 sText_UsedVar2WildRepelled[] = _("{PLAYER} used the\n{STR_VAR_2}.\pWild POKéMON will be repelled.{PAUSE_UNTIL_PRESS}");
static const u8 sText_PlayedPokeFluteCatchy[] = _("Played the POKé FLUTE.\pNow, that's a catchy tune!{PAUSE_UNTIL_PRESS}");
static const u8 sText_PlayedPokeFlute[] = _("Played the POKé FLUTE.");
static const u8 sText_PokeFluteAwakenedMon[] = _("The POKé FLUTE awakened sleeping\nPOKéMON.{PAUSE_UNTIL_PRESS}");

// EWRAM variables
EWRAM_DATA static TaskFunc sItemUseOnFieldCB = NULL;

// Below is set TRUE by UseRegisteredKeyItemOnField
#define tUsingRegisteredKeyItem  data[3]

// UB here if an item with type ITEM_USE_MAIL or ITEM_USE_BAG_MENU uses SetUpItemUseCallback
// Never occurs in vanilla, but can occur with improperly created items
static const MainCallback sItemUseCallbacks[] =
{
    [ITEM_USE_PARTY_MENU - 1]       = CB2_ShowPartyMenuForItemUse,
    [ITEM_USE_FIELD - 1]            = CB2_ReturnToField,
    [ITEM_USE_PBLOCK_CASE - 1]      = NULL,
    [ITEM_USE_PARTY_MENU_MOVES - 1] = CB2_ShowPartyMenuForItemUse,
};

static const u8 sClockwiseDirections[] = {DIR_NORTH, DIR_EAST, DIR_SOUTH, DIR_WEST};

static const struct YesNoFuncTable sUseTMHMYesNoFuncTable =
{
    .yesFunc = UseTMHM,
    .noFunc = CloseItemMessage,
};

#define tEnigmaBerryType data[4]
static void SetUpItemUseCallback(u8 taskId)
{
    enum ItemType type;
    if (gSpecialVar_ItemId == ITEM_ENIGMA_BERRY_E_READER)
        type = gTasks[taskId].tEnigmaBerryType - 1;
    else
        type = GetItemType(gSpecialVar_ItemId) - 1;

    if (gTasks[taskId].tUsingRegisteredKeyItem && type == (ITEM_USE_PARTY_MENU - 1))
    {
        FadeScreen(FADE_TO_BLACK, 0);
        gPartyMenu.data1 = DATA1_PARTY_MENU_FROM_FIELD;
        gTasks[taskId].func = Task_PartyMenuItemUseFromField;
    }
    else
    {
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
        {
            gBagMenu->newScreenCallback = sItemUseCallbacks[type];
            Task_FadeAndCloseBagMenu(taskId);
        }
        else
        {
            gPyramidBagMenu->newScreenCallback = sItemUseCallbacks[type];
            CloseBattlePyramidBag(taskId);
        }
    }
}

static void SetUpItemUseOnFieldCallback(u8 taskId)
{
    if (gTasks[taskId].tUsingRegisteredKeyItem != TRUE)
    {
        gFieldCallback = FieldCB_UseItemOnField;
        SetUpItemUseCallback(taskId);
    }
    else
    {
        sItemUseOnFieldCB(taskId);
    }
}

static void FieldCB_UseItemOnField(void)
{
    FadeInFromBlack();
    CreateTask(Task_CallItemUseOnFieldCallback, 8);
}

static void Task_CallItemUseOnFieldCallback(u8 taskId)
{
    if (IsWeatherNotFadingIn() == 1)
        sItemUseOnFieldCB(taskId);
}

static void Task_PartyMenuItemUseFromField(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        SetMainCallback2(CB2_ShowPartyMenuForItemUse);
        DestroyTask(taskId);
    }
}

static void DisplayCannotUseItemMessage(u8 taskId, bool8 isUsingRegisteredKeyItemOnField, const u8 *str)
{
    StringExpandPlaceholders(gStringVar4, str);
    if (!isUsingRegisteredKeyItemOnField)
    {
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
            DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
        else
            DisplayItemMessageInBattlePyramid(taskId, gText_DadsAdvice, Task_CloseBattlePyramidBagMessage);
    }
    else
    {
        DisplayItemMessageOnField(taskId, gStringVar4, Task_CloseCantUseKeyItemMessage);
    }
}

void DisplayDadsAdviceCannotUseItemMessage(u8 taskId, bool8 isUsingRegisteredKeyItemOnField)
{
    DisplayCannotUseItemMessage(taskId, isUsingRegisteredKeyItemOnField, gText_DadsAdvice);
}

static void DisplayCannotDismountBikeMessage(u8 taskId, bool8 isUsingRegisteredKeyItemOnField)
{
    DisplayCannotUseItemMessage(taskId, isUsingRegisteredKeyItemOnField, sText_CantDismountBike);
}

static void Task_CloseCantUseKeyItemMessage(u8 taskId)
{
    ClearDialogWindowAndFrame(0, TRUE);
    DestroyTask(taskId);
    ScriptUnfreezeObjectEvents();
    UnlockPlayerFieldControls();
}

u8 CheckIfItemIsTMHMOrEvolutionStone(enum Item itemId)
{
    if (GetItemFieldFunc(itemId) == ItemUseOutOfBattle_TMHM)
        return ITEM_IS_TM_HM;
    else if (GetItemFieldFunc(itemId) == ItemUseOutOfBattle_EvolutionStone)
        return ITEM_IS_EVOLUTION_STONE;
    else
        return ITEM_IS_OTHER;
}

// Mail in the bag menu can't have a message but it can be checked (view the mail background, no message)
static void CB2_CheckMail(void)
{
    struct Mail mail;
    mail.itemId = gSpecialVar_ItemId;
    mail.species = SPECIES_NONE;
    ReadMail(&mail, CB2_ReturnToBagMenuPocket, FALSE);
}

void ItemUseOutOfBattle_Mail(u8 taskId)
{
    gBagMenu->newScreenCallback = CB2_CheckMail;
    Task_FadeAndCloseBagMenu(taskId);
}

STATIC_ASSERT(I_EXP_SHARE_ITEM < GEN_6 || I_EXP_SHARE_FLAG > TEMP_FLAGS_END, YouNeedToSetAFlagToUseGen6ExpShare);

void ItemUseOutOfBattle_ExpShare(u8 taskId)
{
#if I_EXP_SHARE_ITEM >= GEN_6
    if (IsGen6ExpShareEnabled())
    {
        PlaySE(SE_PC_OFF);
        if (!gTasks[taskId].data[2]) // to account for pressing select in the overworld
            DisplayItemMessageOnField(taskId, gText_ExpShareOff, Task_CloseCantUseKeyItemMessage);
        else
            DisplayItemMessage(taskId, FONT_NORMAL, gText_ExpShareOff, CloseItemMessage);
    }
    else
    {
        PlaySE(SE_EXP_MAX);
        if (!gTasks[taskId].data[2]) // to account for pressing select in the overworld
            DisplayItemMessageOnField(taskId, gText_ExpShareOn, Task_CloseCantUseKeyItemMessage);
        else
            DisplayItemMessage(taskId, FONT_NORMAL, gText_ExpShareOn, CloseItemMessage);
    }
    FlagToggle(I_EXP_SHARE_FLAG);
#else
    DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
#endif
}

void ItemUseOutOfBattle_Bike(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    s16 coordsY;
    s16 coordsX;
    u8 behavior;
    PlayerGetDestCoords(&coordsX, &coordsY);
    behavior = MapGridGetMetatileBehaviorAt(coordsX, coordsY);
    if (FlagGet(FLAG_SYS_CYCLING_ROAD) == TRUE || MetatileBehavior_IsVerticalRail(behavior) == TRUE || MetatileBehavior_IsHorizontalRail(behavior) == TRUE || MetatileBehavior_IsIsolatedVerticalRail(behavior) == TRUE || MetatileBehavior_IsIsolatedHorizontalRail(behavior) == TRUE)
    {
        DisplayCannotDismountBikeMessage(taskId, tUsingRegisteredKeyItem);
    }
    else if (Overworld_IsBikingAllowed() && !IsBikingDisallowedByPlayer() && FollowerNPCCanBike())
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_Bike;
        SetUpItemUseOnFieldCallback(taskId);
    }
    else
    {
        DisplayDadsAdviceCannotUseItemMessage(taskId, tUsingRegisteredKeyItem);
    }
}

static void ItemUseOnFieldCB_Bike(u8 taskId)
{
    if (GetItemSecondaryId(gSpecialVar_ItemId) == STANDARD_BIKE)
        GetOnOffBike(PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE);
    else if (GetItemSecondaryId(gSpecialVar_ItemId) == MACH_BIKE)
        GetOnOffBike(PLAYER_AVATAR_FLAG_MACH_BIKE);
    else // ACRO_BIKE
        GetOnOffBike(PLAYER_AVATAR_FLAG_ACRO_BIKE);

    FollowerNPC_HandleBike();
    ScriptUnfreezeObjectEvents();
    UnlockPlayerFieldControls();
    DestroyTask(taskId);
}

static bool32 CanFish(void)
{
    s16 x, y;
    u16 tileBehavior;

    GetXYCoordsOneStepInFrontOfPlayer(&x, &y);
    tileBehavior = MapGridGetMetatileBehaviorAt(x, y);

    if (MetatileBehavior_IsWaterfall(tileBehavior))
        return FALSE;

    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_UNDERWATER))
        return FALSE;

    if (!TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING))
    {
        if (IsPlayerFacingSurfableFishableWater())
            return TRUE;
    }
    else
    {
        if (MetatileBehavior_IsSurfableWaterOrUnderwater(tileBehavior) && MapGridGetCollisionAt(x, y) == 0)
            return TRUE;
        if (MetatileBehavior_IsBridgeOverWaterNoEdge(tileBehavior) == TRUE)
            return TRUE;
    }

    return FALSE;
}

void ItemUseOutOfBattle_Rod(u8 taskId)
{
    if (CanFish() == TRUE)
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_Rod;
        SetUpItemUseOnFieldCallback(taskId);
    }
    else
    {
        DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
    }
}

static void ItemUseOnFieldCB_Rod(u8 taskId)
{
    StartFishing(GetItemSecondaryId(gSpecialVar_ItemId));
    DestroyTask(taskId);
}

// Proyecto Sinnoh: Radar Pokémon
static const u8 sText_RadarCharging[] = _("The POKé RADAR's battery is still\ncharging. Walk a little more!");
static const u8 sText_RadarNoGrass[] = _("There's no tall grass around here to\nuse the POKé RADAR on.");

static void ItemUseOnFieldCB_PokeRadar(u8 taskId)
{
    if (!SinnohRadar_Activate())
    {
        DisplayItemMessageOnField(taskId, sText_RadarNoGrass, Task_CloseCantUseKeyItemMessage);
        return;
    }
    ScriptUnfreezeObjectEvents();
    UnlockPlayerFieldControls();
    DestroyTask(taskId);
}

void ItemUseOutOfBattle_PokeRadar(u8 taskId)
{
    if (TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING | PLAYER_AVATAR_FLAG_UNDERWATER | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE))
    {
        DisplayCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem, sText_RadarNoGrass);
        return;
    }
    if (!SinnohRadar_IsCharged())
    {
        DisplayCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem, sText_RadarCharging);
        return;
    }
    sItemUseOnFieldCB = ItemUseOnFieldCB_PokeRadar;
    SetUpItemUseOnFieldCallback(taskId);
}

void ItemUseOutOfBattle_Itemfinder(u8 var)
{
    IncrementGameStat(GAME_STAT_USED_ITEMFINDER);
    sItemUseOnFieldCB = ItemUseOnFieldCB_Itemfinder;
    SetUpItemUseOnFieldCallback(var);
}

static void ItemUseOnFieldCB_Itemfinder(u8 taskId)
{
    if (I_ORAS_DOWSING_FLAG != 0)
    {
        if (!TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) && !TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_UNDERWATER))
            gTasks[taskId].func = Task_UseORASDowsingMachine;
        else
            DisplayItemMessageOnField(taskId, gText_DadsAdvice, Task_CloseItemfinderMessage);
    }
    else
    {
        if (ItemfinderCheckForHiddenItems(gMapHeader.events, taskId) == TRUE)
            gTasks[taskId].func = Task_UseItemfinder;
        else
            DisplayItemMessageOnField(taskId, sText_ItemFinderNothing, Task_CloseItemfinderMessage);
    }
}

// Define itemfinder task data
#define tItemDistanceX    data[0]
#define tItemDistanceY    data[1]
#define tItemFound        data[2]
#define tCounter          data[3] // Used to count delay between beeps and rotations during player spin
#define tItemfinderBeeps  data[4]
#define tFacingDir        data[5]

static void Task_UseItemfinder(u8 taskId)
{
    u8 playerDir;
    u8 playerDirToItem;
    u8 i;
    s16 *data = gTasks[taskId].data;
    if (tCounter == 0)
    {
        if (tItemfinderBeeps == 4)
        {
            playerDirToItem = GetDirectionToHiddenItem(tItemDistanceX, tItemDistanceY);
            if (playerDirToItem != DIR_NONE)
            {
                PlayerFaceHiddenItem(sClockwiseDirections[playerDirToItem - 1]);
                gTasks[taskId].func = Task_HiddenItemNearby;
            }
            else
            {
                // Player is standing on hidden item
                playerDir = GetPlayerFacingDirection();
                for (i = 0; i < ARRAY_COUNT(sClockwiseDirections); i++)
                {
                    if (playerDir == sClockwiseDirections[i])
                        tFacingDir = (i + 1) & 3;
                }
                gTasks[taskId].func = Task_StandingOnHiddenItem;
                tCounter = 0;
                tItemFound = 0;
            }
            return;
        }
        PlaySE(SE_ITEMFINDER);
        tItemfinderBeeps++;
    }
    tCounter = (tCounter + 1) & 0x1F;
}

static void Task_CloseItemfinderMessage(u8 taskId)
{
    ClearDialogWindowAndFrame(0, TRUE);
    ScriptUnfreezeObjectEvents();
    UnlockPlayerFieldControls();
    DestroyTask(taskId);
}

bool8 ItemfinderCheckForHiddenItems(const struct MapEvents *events, u8 taskId)
{
    s16 playerX, playerY, i, distanceX, distanceY;
    PlayerGetDestCoords(&playerX, &playerY);
    if (I_ORAS_DOWSING_FLAG != 0)
        gSprites[gObjectEvents[gPlayerAvatar.objectEventId].fieldEffectSpriteId].tItemFound = FALSE;
    else
        gTasks[taskId].tItemFound = FALSE;

    for (i = 0; i < events->bgEventCount; i++)
    {
        // Check if there are any hidden items on the current map that haven't been picked up
        if (events->bgEvents[i].kind == BG_EVENT_HIDDEN_ITEM && !FlagGet(HIDDEN_ITEM_FLAG(events->bgEvents[i].bgUnion.hiddenItem.hiddenItemId)))
        {
            distanceX = events->bgEvents[i].x + MAP_OFFSET - playerX;
            distanceY = events->bgEvents[i].y + MAP_OFFSET - playerY;

            // Player can see 7 metatiles on either side horizontally
            // and 5 metatiles on either side vertically
            if (distanceX >= -7 && distanceX <= 7 && distanceY >= -5 && distanceY <= 5)
                SetDistanceOfClosestHiddenItem(taskId, distanceX, distanceY);
        }
    }

    CheckForHiddenItemsInMapConnection(taskId);
    if (gTasks[taskId].tItemFound == TRUE || gSprites[gObjectEvents[gPlayerAvatar.objectEventId].fieldEffectSpriteId].tItemFound)
        return TRUE;
    else
        return FALSE;
}

static bool8 IsHiddenItemPresentAtCoords(const struct MapEvents *events, s16 x, s16 y)
{
    u8 bgEventCount = events->bgEventCount;
    const struct BgEvent *bgEvent = events->bgEvents;
    int i;

    for (i = 0; i < bgEventCount; i++)
    {
        if (bgEvent[i].kind == BG_EVENT_HIDDEN_ITEM && x == bgEvent[i].x && y == bgEvent[i].y) // hidden item and coordinates matches x and y passed?
        {
            if (!FlagGet(HIDDEN_ITEM_FLAG(bgEvent[i].bgUnion.hiddenItem.hiddenItemId)))
                return TRUE;
            else
                return FALSE;
        }
    }
    return FALSE;
}

static bool8 IsHiddenItemPresentInConnection(const struct MapConnection *connection, int x, int y)
{
    s16 connectionX, connectionY;
    struct MapHeader const *const connectionHeader = GetMapHeaderFromConnection(connection);

// To convert our x/y into coordinates that are relative to the connected map, we must:
//  - Subtract the virtual offset used for the border buffer (MAP_OFFSET).
//  - Subtract the horizontal offset between North/South connections, or the vertical offset for East/West
//  - Account for map size. (0,0) is in the NW corner of our map, so when looking North/West we have to add the height/width of the connected map,
//     and when looking South/East we have to subtract the height/width of our current map.
#define localX (x - MAP_OFFSET)
#define localY (y - MAP_OFFSET)
    switch (connection->direction)
    {
    case CONNECTION_NORTH:
        connectionX = localX - connection->offset;
        connectionY = connectionHeader->mapLayout->height + localY;
        break;
    case CONNECTION_SOUTH:
        connectionX = localX - connection->offset;
        connectionY = localY - gMapHeader.mapLayout->height;
        break;
    case CONNECTION_WEST:
        connectionX = connectionHeader->mapLayout->width + localX;
        connectionY = localY - connection->offset;
        break;
    case CONNECTION_EAST:
        connectionX = localX - gMapHeader.mapLayout->width;
        connectionY = localY - connection->offset;
        break;
    default:
        return FALSE;
    }
    return IsHiddenItemPresentAtCoords(connectionHeader->events, connectionX, connectionY);
}

#undef localX
#undef localY

static void CheckForHiddenItemsInMapConnection(u8 taskId)
{
    s16 playerX, playerY;
    s16 x, y;
    s16 width = gMapHeader.mapLayout->width + MAP_OFFSET;
    s16 height = gMapHeader.mapLayout->height + MAP_OFFSET;

    s16 var1 = MAP_OFFSET;
    s16 var2 = MAP_OFFSET;

    PlayerGetDestCoords(&playerX, &playerY);

    // Player can see 7 metatiles on either side horizontally
    // and 5 metatiles on either side vertically
    for (x = playerX - 7; x <= playerX + 7; x++)
    {
        for (y = playerY - 5; y <= playerY + 5; y++)
        {
            if (var1 > x
             || x >= width
             || var2 > y
             || y >= height)
            {
                const struct MapConnection *conn = GetMapConnectionAtPos(x, y);
                if (conn && IsHiddenItemPresentInConnection(conn, x, y) == TRUE)
                    SetDistanceOfClosestHiddenItem(taskId, x - playerX, y - playerY);
            }
        }
    }
}

static void SetDistanceOfClosestHiddenItem(u8 taskId, s16 itemDistanceX, s16 itemDistanceY)
{
    s16 *data = gTasks[taskId].data;
    s16 oldItemAbsX, oldItemAbsY, newItemAbsX, newItemAbsY;
    if (I_ORAS_DOWSING_FLAG != 0)
        data = gSprites[gObjectEvents[gPlayerAvatar.objectEventId].fieldEffectSpriteId].data;

    if (tItemFound == FALSE)
    {
        // No other items found yet, set this one
        tItemDistanceX = itemDistanceX;
        tItemDistanceY = itemDistanceY;
        tItemFound = TRUE;
        return;
    }

    // Other items have been found, check if this one is closer

    // Get absolute x distance of the already-found item
    if (tItemDistanceX < 0)
        oldItemAbsX = tItemDistanceX * -1; // WEST
    else
        oldItemAbsX = tItemDistanceX; // EAST

    // Get absolute y distance of the already-found item
    if (tItemDistanceY < 0)
        oldItemAbsY = tItemDistanceY * -1; // NORTH
    else
        oldItemAbsY = tItemDistanceY; // SOUTH

    // Get absolute x distance of the newly-found item
    if (itemDistanceX < 0)
        newItemAbsX = itemDistanceX * -1;
    else
        newItemAbsX = itemDistanceX;

    // Get absolute y distance of the newly-found item
    if (itemDistanceY < 0)
        newItemAbsY = itemDistanceY * -1;
    else
        newItemAbsY = itemDistanceY;

    if (oldItemAbsX + oldItemAbsY > newItemAbsX + newItemAbsY)
    {
        // New item is closer
        tItemDistanceX = itemDistanceX;
        tItemDistanceY = itemDistanceY;
    }
    else if (oldItemAbsX + oldItemAbsY == newItemAbsX + newItemAbsY
          && (oldItemAbsY > newItemAbsY || (oldItemAbsY == newItemAbsY && tItemDistanceY < itemDistanceY)))
    {
        // If items are equal distance, use whichever is closer on the Y axis or further south
        tItemDistanceX = itemDistanceX;
        tItemDistanceY = itemDistanceY;
    }
}

enum Direction GetDirectionToHiddenItem(s16 itemDistanceX, s16 itemDistanceY)
{
    s16 absX, absY;

    if (itemDistanceX == 0 && itemDistanceY == 0)
        return DIR_NONE; // player is standing on the item.

    // Get absolute X distance.
    if (itemDistanceX < 0)
        absX = itemDistanceX * -1;
    else
        absX = itemDistanceX;

    // Get absolute Y distance.
    if (itemDistanceY < 0)
        absY = itemDistanceY * -1;
    else
        absY = itemDistanceY;

    if (absX > absY)
    {
        if (itemDistanceX < 0)
            return DIR_EAST;
        else
            return DIR_NORTH;
    }
    else if (absX < absY)
    {
        if (itemDistanceY < 0)
            return DIR_SOUTH;
        else
            return DIR_WEST;
    }
    else if (absX == absY)
    {
        if (itemDistanceY < 0)
            return DIR_SOUTH;
        else
            return DIR_WEST;
    }
    else
    {
        return DIR_NONE; // Unreachable
    }
}

static void PlayerFaceHiddenItem(enum Direction direction)
{
    ObjectEventClearHeldMovementIfFinished(&gObjectEvents[GetObjectEventIdByLocalIdAndMap(LOCALID_PLAYER, 0, 0)]);
    ObjectEventClearHeldMovement(&gObjectEvents[GetObjectEventIdByLocalIdAndMap(LOCALID_PLAYER, 0, 0)]);
    UnfreezeObjectEvent(&gObjectEvents[GetObjectEventIdByLocalIdAndMap(LOCALID_PLAYER, 0, 0)]);
    PlayerTurnInPlace(direction);
}

static void Task_HiddenItemNearby(u8 taskId)
{
    if (ObjectEventCheckHeldMovementStatus(&gObjectEvents[GetObjectEventIdByLocalIdAndMap(LOCALID_PLAYER, 0, 0)]) == TRUE)
        DisplayItemMessageOnField(taskId, sText_ItemFinderNearby, Task_CloseItemfinderMessage);
}

static void Task_StandingOnHiddenItem(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (ObjectEventCheckHeldMovementStatus(&gObjectEvents[GetObjectEventIdByLocalIdAndMap(LOCALID_PLAYER, 0, 0)]) == TRUE
    || tItemFound == FALSE)
    {
        // Spin player around on item
        PlayerFaceHiddenItem(sClockwiseDirections[tFacingDir]);
        tItemFound = TRUE;
        tFacingDir = (tFacingDir + 1) & 3;
        tCounter++;

        if (tCounter == 4)
            DisplayItemMessageOnField(taskId, sText_ItemFinderOnTop, Task_CloseItemfinderMessage);
    }
}

// Undefine itemfinder task data
#undef tItemDistanceX
#undef tItemDistanceY
#undef tItemFound
#undef tCounter
#undef tItemfinderBeeps
#undef tFacingDir

void ItemUseOutOfBattle_PokeblockCase(u8 taskId)
{
    if (MenuHelpers_IsLinkActive() == TRUE)
    {
        DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
    }
    else if (gTasks[taskId].tUsingRegisteredKeyItem != TRUE)
    {
        gBagMenu->newScreenCallback = CB2_OpenPokeblockFromBag;
        Task_FadeAndCloseBagMenu(taskId);
    }
    else
    {
        gFieldCallback = FieldCB_ReturnToFieldNoScript;
        FadeScreen(FADE_TO_BLACK, 0);
        gTasks[taskId].func = Task_OpenRegisteredPokeblockCase;
    }
}

static void CB2_OpenPokeblockFromBag(void)
{
    OpenPokeblockCase(PBLOCK_CASE_FIELD, CB2_ReturnToBagMenuPocket);
}

static void Task_OpenRegisteredPokeblockCase(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        OpenPokeblockCase(PBLOCK_CASE_FIELD, CB2_ReturnToField);
        DestroyTask(taskId);
    }
}

void ItemUseOutOfBattle_PokemonBoxLink(u8 taskId)
{
    sItemUseOnFieldCB = Task_AccessPokemonBoxLink;
    SetUpItemUseOnFieldCallback(taskId);
}

static void Task_AccessPokemonBoxLink(u8 taskId)
{
    ScriptContext_SetupScript(EventScript_AccessPokemonBoxLink);
    DestroyTask(taskId);
}

// Pokémon World: open the Badge Case screen, from the bag or as a registered item.
static void CB2_OpenBadgeCaseFromBag(void)
{
    ShowBadgeCase(CB2_ReturnToBagMenuPocket);
}

static void Task_OpenRegisteredBadgeCase(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        CleanupOverworldWindowsAndTilemaps();
        ShowBadgeCase(CB2_ReturnToField);
        DestroyTask(taskId);
    }
}

void ItemUseOutOfBattle_BadgeCase(u8 taskId)
{
    if (!gTasks[taskId].tUsingRegisteredKeyItem)
    {
        gBagMenu->newScreenCallback = CB2_OpenBadgeCaseFromBag;
        Task_FadeAndCloseBagMenu(taskId);
    }
    else
    {
        gFieldCallback = FieldCB_ReturnToFieldNoScript;
        FadeScreen(FADE_TO_BLACK, 0);
        gTasks[taskId].func = Task_OpenRegisteredBadgeCase;
    }
}

void ItemUseOutOfBattle_CoinCase(u8 taskId)
{
    ConvertIntToDecimalStringN(gStringVar1, GetCoins(), STR_CONV_MODE_LEFT_ALIGN, 4);
    StringExpandPlaceholders(gStringVar4, sText_CoinCase);

    if (!gTasks[taskId].tUsingRegisteredKeyItem)
    {
        DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
    }
    else
    {
        DisplayItemMessageOnField(taskId, gStringVar4, Task_CloseCantUseKeyItemMessage);
    }
}

void ItemUseOutOfBattle_PowderJar(u8 taskId)
{
    ConvertIntToDecimalStringN(gStringVar1, GetBerryPowder(), STR_CONV_MODE_LEFT_ALIGN, 5);
    StringExpandPlaceholders(gStringVar4, sText_PowderQty);

    if (!gTasks[taskId].tUsingRegisteredKeyItem)
    {
        DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
    }
    else
    {
        DisplayItemMessageOnField(taskId, gStringVar4, Task_CloseCantUseKeyItemMessage);
    }
}

void ItemUseOutOfBattle_Berry(u8 taskId)
{
    if (IsPlayerFacingEmptyBerryTreePatch() == TRUE)
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_Berry;
        gFieldCallback = FieldCB_UseItemOnField;
        gBagMenu->newScreenCallback = CB2_ReturnToField;
        Task_FadeAndCloseBagMenu(taskId);
    }
    else
    {
        GetItemFieldFunc(gSpecialVar_ItemId)(taskId);
    }
}

static void ItemUseOnFieldCB_Berry(u8 taskId)
{
    RemoveBagItem(gSpecialVar_ItemId, 1);
    LockPlayerFieldControls();
    ScriptContext_SetupScript(BerryTree_EventScript_ItemUsePlantBerry);
    DestroyTask(taskId);
}

void ItemUseOutOfBattle_WailmerPail(u8 taskId)
{
    if (TryToWaterSudowoodo() == TRUE)
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_WailmerPailSudowoodo;
        SetUpItemUseOnFieldCallback(taskId);
    }
    else if (TryToWaterBerryTree() == TRUE)
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_WailmerPailBerry;
        SetUpItemUseOnFieldCallback(taskId);
    }
    else
    {
        DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
    }
}

static void ItemUseOnFieldCB_WailmerPailBerry(u8 taskId)
{
    LockPlayerFieldControls();
    ScriptContext_SetupScript(BerryTree_EventScript_ItemUseWailmerPail);
    DestroyTask(taskId);
}

static bool8 TryToWaterSudowoodo(void)
{
    s16 x, y;
    u8 elevation;
    u8 objId;
    GetXYCoordsOneStepInFrontOfPlayer(&x, &y);
    elevation = PlayerGetElevation();
    objId = GetObjectEventIdByPosition(x, y, elevation);
    if (objId == OBJECT_EVENTS_COUNT || gObjectEvents[objId].graphicsId != OBJ_EVENT_GFX_SUDOWOODO)
        return FALSE;
    else
        return TRUE;
}

static void ItemUseOnFieldCB_WailmerPailSudowoodo(u8 taskId)
{
    LockPlayerFieldControls();
    ScriptContext_SetupScript(BattleFrontier_OutsideEast_EventScript_WaterSudowoodo);
    DestroyTask(taskId);
}

void ItemUseOutOfBattle_Medicine(u8 taskId)
{
    gItemUseCB = ItemUseCB_Medicine;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_AbilityCapsule(u8 taskId)
{
    gItemUseCB = ItemUseCB_AbilityCapsule;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_AbilityPatch(u8 taskId)
{
    gItemUseCB = ItemUseCB_AbilityPatch;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_Mint(u8 taskId)
{
    gItemUseCB = ItemUseCB_Mint;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_ResetEVs(u8 taskId)
{
    gItemUseCB = ItemUseCB_ResetEVs;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_ReduceEV(u8 taskId)
{
    gItemUseCB = ItemUseCB_ReduceEV;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_SacredAsh(u8 taskId)
{
    gItemUseCB = ItemUseCB_SacredAsh;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_PPRecovery(u8 taskId)
{
    gItemUseCB = ItemUseCB_PPRecovery;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_PPUp(u8 taskId)
{
    gItemUseCB = ItemUseCB_PPUp;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_RareCandy(u8 taskId)
{
    gItemUseCB = ItemUseCB_RareCandy;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_DynamaxCandy(u8 taskId)
{
    gItemUseCB = ItemUseCB_DynamaxCandy;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_TMHM(u8 taskId)
{
    if (GetItemTMHMIndex(gSpecialVar_ItemId) > NUM_TECHNICAL_MACHINES)
        DisplayItemMessage(taskId, FONT_NORMAL, sText_BootedUpHM, BootUpSoundTMHM); // HM
    else
        DisplayItemMessage(taskId, FONT_NORMAL, sText_BootedUpTM, BootUpSoundTMHM); // TM
}

static void BootUpSoundTMHM(u8 taskId)
{
    PlaySE(SE_PC_LOGIN);
    gTasks[taskId].func = Task_ShowTMHMContainedMessage;
}

static void Task_ShowTMHMContainedMessage(u8 taskId)
{
    if (JOY_NEW(A_BUTTON | B_BUTTON))
    {
        StringCopy(gStringVar1, GetMoveName(ItemIdToBattleMoveId(gSpecialVar_ItemId)));
        StringExpandPlaceholders(gStringVar4, sText_TMHMContainedVar1);
        DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, UseTMHMYesNo);
    }
}

static void UseTMHMYesNo(u8 taskId)
{
    BagMenu_YesNo(taskId, ITEMWIN_YESNO_HIGH, &sUseTMHMYesNoFuncTable);
}

static void UseTMHM(u8 taskId)
{
    gItemUseCB = ItemUseCB_TMHM;
    SetUpItemUseCallback(taskId);
}

static void RemoveUsedItem(void)
{
    RemoveBagItem(gSpecialVar_ItemId, 1);
    CopyItemName(gSpecialVar_ItemId, gStringVar2);
    StringExpandPlaceholders(gStringVar4, gText_PlayerUsedVar2);
    if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
    {
        UpdatePocketItemList(GetItemPocket(gSpecialVar_ItemId));
        UpdatePocketListPosition(GetItemPocket(gSpecialVar_ItemId));
    }
    else
    {
        UpdatePyramidBagList();
        UpdatePyramidBagCursorPos();
    }
}

void ItemUseOutOfBattle_Repel(u8 taskId)
{
    if (REPEL_STEP_COUNT == 0)
        gTasks[taskId].func = Task_StartUseRepel;
    else if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
        DisplayItemMessage(taskId, FONT_NORMAL, gText_RepelEffectsLingered, CloseItemMessage);
    else
        DisplayItemMessageInBattlePyramid(taskId, gText_RepelEffectsLingered, Task_CloseBattlePyramidBagMessage);
}

static void Task_StartUseRepel(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (++data[8] > 7)
    {
        data[8] = 0;
        PlaySE(SE_REPEL);
        gTasks[taskId].func = Task_UseRepel;
    }
}

static void Task_UseRepel(u8 taskId)
{
    if (!IsSEPlaying())
    {
        VarSet(VAR_REPEL_STEP_COUNT, GetItemHoldEffectParam(gSpecialVar_ItemId));
    #if VAR_LAST_REPEL_LURE_USED != 0
        VarSet(VAR_LAST_REPEL_LURE_USED, gSpecialVar_ItemId);
    #endif
        RemoveUsedItem();
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
            DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
        else
            DisplayItemMessageInBattlePyramid(taskId, gStringVar4, Task_CloseBattlePyramidBagMessage);
    }
}

void ItemUseOutOfBattle_Lure(u8 taskId)
{
    if (LURE_STEP_COUNT == 0)
        gTasks[taskId].func = Task_StartUseLure;
    else if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
        DisplayItemMessage(taskId, FONT_NORMAL, gText_LureEffectsLingered, CloseItemMessage);
    else
        DisplayItemMessageInBattlePyramid(taskId, gText_LureEffectsLingered, Task_CloseBattlePyramidBagMessage);
}

static void Task_StartUseLure(u8 taskId)
{
    s16* data = gTasks[taskId].data;

    if (++data[8] > 7)
    {
        data[8] = 0;
        PlaySE(SE_REPEL);
        gTasks[taskId].func = Task_UseLure;
    }
}

static void Task_UseLure(u8 taskId)
{
    if (!IsSEPlaying())
    {
        VarSet(VAR_REPEL_STEP_COUNT, GetItemHoldEffectParam(gSpecialVar_ItemId) | REPEL_LURE_MASK);
    #if VAR_LAST_REPEL_LURE_USED != 0
        VarSet(VAR_LAST_REPEL_LURE_USED, gSpecialVar_ItemId);
    #endif
        RemoveUsedItem();
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
            DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
        else
            DisplayItemMessageInBattlePyramid(taskId, gStringVar4, Task_CloseBattlePyramidBagMessage);
    }
}

static void Task_UsedBlackWhiteFlute(u8 taskId)
{
    if (++gTasks[taskId].data[8] > 7)
    {
        PlaySE(SE_GLASS_FLUTE);
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
            DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
        else
            DisplayItemMessageInBattlePyramid(taskId, gStringVar4, Task_CloseBattlePyramidBagMessage);
    }
}

void ItemUseOutOfBattle_BlackWhiteFlute(u8 taskId)
{
    CopyItemName(gSpecialVar_ItemId, gStringVar2);
    if (gSpecialVar_ItemId == ITEM_WHITE_FLUTE)
    {
        FlagSet(FLAG_SYS_ENC_UP_ITEM);
        FlagClear(FLAG_SYS_ENC_DOWN_ITEM);
        StringExpandPlaceholders(gStringVar4, sText_UsedVar2WildLured);
    }
    else
    {
        FlagSet(FLAG_SYS_ENC_DOWN_ITEM);
        FlagClear(FLAG_SYS_ENC_UP_ITEM);
        StringExpandPlaceholders(gStringVar4, sText_UsedVar2WildRepelled);
    }
    gTasks[taskId].data[8] = 0;
    gTasks[taskId].func = Task_UsedBlackWhiteFlute;
}

void Task_UseDigEscapeRopeOnField(u8 taskId)
{
    ResetInitialPlayerAvatarState();
    StartEscapeRopeFieldEffect();
    DestroyTask(taskId);
}

static void ItemUseOnFieldCB_EscapeRope(u8 taskId)
{
    Overworld_ResetStateAfterDigEscRope();
    if (I_KEY_ESCAPE_ROPE < GEN_8)
        RemoveBagItem(gSpecialVar_ItemId, 1);

    CopyItemName(gSpecialVar_ItemId, gStringVar2);
    StringExpandPlaceholders(gStringVar4, gText_PlayerUsedVar2);
    gTasks[taskId].data[0] = 0;
    DisplayItemMessageOnField(taskId, gStringVar4, Task_UseDigEscapeRopeOnField);
}

bool8 CanUseDigOrEscapeRopeOnCurMap(void)
{
    if (!CheckFollowerNPCFlag(FOLLOWER_NPC_FLAG_CAN_LEAVE_ROUTE))
        return FALSE;

    if (gMapHeader.allowEscaping)
        return TRUE;
    else
        return FALSE;
}

void ItemUseOutOfBattle_EscapeRope(u8 taskId)
{
    if (CanUseDigOrEscapeRopeOnCurMap() == TRUE)
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_EscapeRope;
        SetUpItemUseOnFieldCallback(taskId);
    }
    else
    {
        DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
    }
}

void ItemUseOutOfBattle_EvolutionStone(u8 taskId)
{
    gItemUseCB = ItemUseCB_EvolutionStone;
    SetUpItemUseCallback(taskId);
}

static u32 GetBallThrowableState(void)
{
    if (IsBattlerAlive(GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT))
     && IsBattlerAlive(GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT)))
        return BALL_THROW_UNABLE_TWO_MONS;
    else if (IsPlayerPartyAndPokemonStorageFull() == TRUE)
        return BALL_THROW_UNABLE_NO_ROOM;
    else if (GetConfig(B_SEMI_INVULNERABLE_CATCH) >= GEN_4 &&  IsSemiInvulnerable(GetCatchingBattler(), CHECK_ALL))
        return BALL_THROW_UNABLE_SEMI_INVULNERABLE;
    else if (FlagGet(WE_FLAG_NO_CATCHING) || !IsAllowedToUseBag())
        return BALL_THROW_UNABLE_DISABLED_FLAG;

    return BALL_THROW_ABLE;
}

bool32 CanThrowBall(void)
{
    return (GetBallThrowableState() == BALL_THROW_ABLE);
}

static const u8 sText_CantThrowPokeBall_TwoMons[] = _("Cannot throw a ball!\nThere are two Pokémon out there!\p");
static const u8 sText_CantThrowPokeBall_SemiInvulnerable[] = _("Cannot throw a ball!\nThere's no Pokémon in sight!\p");
static const u8 sText_CantThrowPokeBall_Disabled[] = _("POKé BALLS cannot be used\nright now!\p");

static void ItemUseInBattle_ShowPartyMenu(u8 taskId)
{
    if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
    {
        gBagMenu->newScreenCallback = ChooseMonForInBattleItem;
        Task_FadeAndCloseBagMenu(taskId);
    }
    else
    {
        gPyramidBagMenu->newScreenCallback = ChooseMonForInBattleItem;
        CloseBattlePyramidBag(taskId);
    }
}

void ItemUseInBattle_PartyMenu(u8 taskId)
{
    gItemUseCB = ItemUseCB_BattleScript;
    ItemUseInBattle_ShowPartyMenu(taskId);
}

void ItemUseInBattle_PartyMenuChooseMove(u8 taskId)
{
    gItemUseCB = ItemUseCB_BattleChooseMove;
    ItemUseInBattle_ShowPartyMenu(taskId);
}

static bool32 IteamHealsMonVolatile(enum BattlerId battler, enum Item itemId)
{
    const u8 *effect = GetItemEffect(itemId);
    if (effect[3] & ITEM3_STATUS_ALL)
        return (gBattleMons[battler].volatiles.infatuation || gBattleMons[battler].volatiles.confusionTimer > 0);
    else if (effect[0] & ITEM0_INFATUATION)
        return gBattleMons[battler].volatiles.infatuation;
    else if (effect[3] & ITEM3_CONFUSION)
        return gBattleMons[battler].volatiles.confusionTimer > 0;

    return FALSE;
}

static bool32 SelectedMonHasVolatile(enum Item itemId)
{
    if (gPartyMenu.slotId == 0)
        return IteamHealsMonVolatile(0, itemId);
    else if (gBattleTypeFlags & (BATTLE_TYPE_DOUBLE | BATTLE_TYPE_MULTI) && gPartyMenu.slotId == 1)
        return IteamHealsMonVolatile(2, itemId);
    return FALSE;
}

// Returns whether an item can be used in battle and sets the fail text.
bool32 CannotUseItemsInBattle(enum Item itemId, struct Pokemon *mon)
{
    enum EffectItem battleUsage = GetItemBattleUsage(itemId);
    bool8 cannotUse = FALSE;
    const u8* failStr = NULL;
    u32 i, battlerTarget;
    u16 hp = GetMonData(mon, MON_DATA_HP);

    if (gPartyMenu.slotId == 0)
        battlerTarget = B_POSITION_PLAYER_LEFT;
    else if (gPartyMenu.slotId == 1)
        battlerTarget = B_POSITION_PLAYER_RIGHT;
    else
        battlerTarget = MAX_POSITION_COUNT;

    // Embargo Check
    if (battlerTarget < MAX_POSITION_COUNT && GetItemType(itemId) != ITEM_USE_BAG_MENU)
    {
        if (gBattleMons[battlerTarget].volatiles.embargoTimer)
            return TRUE;
    }

    // battleUsage checks
    switch (battleUsage)
    {
    case EFFECT_ITEM_INCREASE_STAT:
        if (hp == 0 || gPartyMenu.slotId > 1)
            cannotUse = TRUE;
        else if (CompareStat(battlerTarget, GetItemEffect(itemId)[1], MAX_STAT_STAGE, CMP_EQUAL, GetBattlerAbility(battlerTarget)))
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_SET_FOCUS_ENERGY:
        if (hp == 0 ||gPartyMenu.slotId > 1)
            cannotUse = TRUE;
        else if (gBattleMons[battlerTarget].volatiles.dragonCheer || gBattleMons[battlerTarget].volatiles.focusEnergy)
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_SET_MIST:
        if (gSideStatuses[GetBattlerSide(gBattlerInMenuId)] & SIDE_STATUS_MIST)
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_ESCAPE:
        if (gBattleTypeFlags & BATTLE_TYPE_TRAINER)
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_THROW_BALL:
        switch (GetBallThrowableState())
        {
        case BALL_THROW_UNABLE_TWO_MONS:
            failStr = sText_CantThrowPokeBall_TwoMons;
            cannotUse = TRUE;
            break;
        case BALL_THROW_UNABLE_NO_ROOM:
            failStr = gText_BoxFull;
            cannotUse = TRUE;
            break;
        case BALL_THROW_UNABLE_SEMI_INVULNERABLE:
            failStr = sText_CantThrowPokeBall_SemiInvulnerable;
            cannotUse = TRUE;
            break;
        case BALL_THROW_UNABLE_DISABLED_FLAG:
            failStr = sText_CantThrowPokeBall_Disabled;
            cannotUse = TRUE;
            break;
        }
        break;
    case EFFECT_ITEM_INCREASE_ALL_STATS:
    // Never called
    {
        if (hp == 0 || gPartyMenu.slotId > 1)
        {
            cannotUse = TRUE;
            break;
        }
        enum Ability ability = GetBattlerAbility(battlerTarget);
        for (i = STAT_ATK; i < NUM_STATS; i++)
        {
            if (CompareStat(battlerTarget, i, MAX_STAT_STAGE, CMP_EQUAL, ability))
            {
                cannotUse = FALSE;
                break;
            }
        }
        break;
    }
    case EFFECT_ITEM_RESTORE_HP:
        if (hp == 0 || hp == GetMonData(mon, MON_DATA_MAX_HP))
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_CURE_STATUS:
        if (!((GetMonData(mon, MON_DATA_STATUS) & GetItemStatus1Mask(itemId))
            || SelectedMonHasVolatile(itemId)))
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_HEAL_AND_CURE_STATUS:
        if ((hp == 0 || hp == GetMonData(mon, MON_DATA_MAX_HP))
            && !((GetMonData(mon, MON_DATA_STATUS) & GetItemStatus1Mask(itemId))
            || SelectedMonHasVolatile(itemId)))
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_REVIVE:
        if (hp != 0)
            cannotUse = TRUE;
        break;
    case EFFECT_ITEM_RESTORE_PP:
        if (GetItemEffect(itemId)[4] == ITEM4_HEAL_PP)
        {
            for (i = 0; i < MAX_MON_MOVES; i++)
            {
                if (GetMonData(mon, MON_DATA_PP1 + i) < CalculatePPWithBonus(GetMonData(mon, MON_DATA_MOVE1 + i), GetMonData(mon, MON_DATA_PP_BONUSES), i))
                    break;
            }
            if (i == MAX_MON_MOVES)
                cannotUse = TRUE;
        }
        else if (GetMonData(mon, MON_DATA_PP1 + gPartyMenu.data1) == CalculatePPWithBonus(GetMonData(mon, MON_DATA_MOVE1 + gPartyMenu.data1), GetMonData(mon, MON_DATA_PP_BONUSES), gPartyMenu.data1))
        {
            cannotUse = TRUE;
        }
        break;
    case EFFECT_ITEM_USE_POKE_FLUTE:
        // ISSUE #10182
        break;
    }

    if (failStr != NULL)
        StringExpandPlaceholders(gStringVar4, failStr);
    else
        StringExpandPlaceholders(gStringVar4, gText_WontHaveEffect);

    return cannotUse;
}

void ItemUseInBattle_BagMenu(u8 taskId)
{
    gPartyMenu.slotId = gBattleStruct->itemPartyIndex[gBattlerInMenuId] = gBattlerPartyIndexes[gBattlerInMenuId];
    if (CannotUseItemsInBattle(gSpecialVar_ItemId, NULL))
    {
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
            DisplayItemMessage(taskId, FONT_NORMAL, gStringVar4, CloseItemMessage);
        else
            DisplayItemMessageInBattlePyramid(taskId, gStringVar4, Task_CloseBattlePyramidBagMessage);
    }
    else
    {
        PlaySE(SE_SELECT);
        if (!GetItemImportance(gSpecialVar_ItemId) && !(B_TRY_CATCH_TRAINER_BALL >= GEN_4 && (GetItemBattleUsage(gSpecialVar_ItemId) == EFFECT_ITEM_THROW_BALL) && (gBattleTypeFlags & BATTLE_TYPE_TRAINER)))
            RemoveUsedItem();
        ScheduleBgCopyTilemapToVram(2);
        if (CurrentBattlePyramidLocation() == PYRAMID_LOCATION_NONE)
            gTasks[taskId].func = Task_FadeAndCloseBagMenu;
        else
            gTasks[taskId].func = CloseBattlePyramidBag;
    }
}

void ItemUseOutOfBattle_EnigmaBerry(u8 taskId)
{
    switch (GetItemEffectType(gSpecialVar_ItemId))
    {
    case ITEM_EFFECT_HEAL_HP:
    case ITEM_EFFECT_CURE_POISON:
    case ITEM_EFFECT_CURE_SLEEP:
    case ITEM_EFFECT_CURE_BURN:
    case ITEM_EFFECT_CURE_FREEZE_FROSTBITE:
    case ITEM_EFFECT_CURE_PARALYSIS:
    case ITEM_EFFECT_CURE_ALL_STATUS:
    case ITEM_EFFECT_ATK_EV:
    case ITEM_EFFECT_HP_EV:
    case ITEM_EFFECT_SPATK_EV:
    case ITEM_EFFECT_SPDEF_EV:
    case ITEM_EFFECT_SPEED_EV:
    case ITEM_EFFECT_DEF_EV:
        gTasks[taskId].tEnigmaBerryType = ITEM_USE_PARTY_MENU;
        ItemUseOutOfBattle_Medicine(taskId);
        break;
    case ITEM_EFFECT_SACRED_ASH:
        gTasks[taskId].tEnigmaBerryType = ITEM_USE_PARTY_MENU;
        ItemUseOutOfBattle_SacredAsh(taskId);
        break;
    case ITEM_EFFECT_RAISE_LEVEL:
        gTasks[taskId].tEnigmaBerryType = ITEM_USE_PARTY_MENU;
        ItemUseOutOfBattle_RareCandy(taskId);
        break;
    case ITEM_EFFECT_PP_UP:
    case ITEM_EFFECT_PP_MAX:
        gTasks[taskId].tEnigmaBerryType = ITEM_USE_PARTY_MENU;
        ItemUseOutOfBattle_PPUp(taskId);
        break;
    case ITEM_EFFECT_HEAL_PP:
        gTasks[taskId].tEnigmaBerryType = ITEM_USE_PARTY_MENU;
        ItemUseOutOfBattle_PPRecovery(taskId);
        break;
    default:
        gTasks[taskId].tEnigmaBerryType = ITEM_USE_BAG_MENU;
        ItemUseOutOfBattle_CannotUse(taskId);
        break;
    }
}

void ItemUseOutOfBattle_FormChange(u8 taskId)
{
    gItemUseCB = ItemUseCB_FormChange;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_FormChange_ConsumedOnUse(u8 taskId)
{
    gItemUseCB = ItemUseCB_FormChange_ConsumedOnUse;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_RotomCatalog(u8 taskId)
{
    gItemUseCB = ItemUseCB_RotomCatalog;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_ZygardeCube(u8 taskId)
{
    gItemUseCB = ItemUseCB_ZygardeCube;
    SetUpItemUseCallback(taskId);
}

void ItemUseOutOfBattle_Fusion(u8 taskId)
{
    gItemUseCB = ItemUseCB_Fusion;
    SetUpItemUseCallback(taskId);
}

void Task_UseHoneyOnField(u8 taskId)
{
    StartSweetScentFieldEffect();
    DestroyTask(taskId);
}

static void ItemUseOnFieldCB_Honey(u8 taskId)
{
    ResetInitialPlayerAvatarState();
    RemoveBagItem(gSpecialVar_ItemId, 1);
    CopyItemName(gSpecialVar_ItemId, gStringVar2);
    StringExpandPlaceholders(gStringVar4, gText_PlayerUsedVar2);
    DisplayItemMessageOnField(taskId, gStringVar4, Task_UseHoneyOnField);
}

void ItemUseOutOfBattle_Honey(u8 taskId)
{
    sItemUseOnFieldCB = ItemUseOnFieldCB_Honey;
    gFieldCallback = FieldCB_UseItemOnField;
    gBagMenu->newScreenCallback = CB2_ReturnToField;
    Task_FadeAndCloseBagMenu(taskId);
}

void ItemUseOutOfBattle_CannotUse(u8 taskId)
{
    DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
}

static bool32 IsValidLocationForVsSeeker(void)
{
    u16 mapGroup = gSaveBlock1Ptr->location.mapGroup;
    u16 mapNum = gSaveBlock1Ptr->location.mapNum;
    enum MapType mapType = gMapHeader.mapType;

    typedef struct {
        u16 mapGroup;
        u16 mapNum;
    } Location;

    u32 i;
    Location validIndoorLocations[] =
    {
        { MAP_GROUP(MAP_MT_PYRE_SUMMIT),           MAP_NUM(MAP_MT_PYRE_SUMMIT) },
        { MAP_GROUP(MAP_SAFARI_ZONE_NORTH),        MAP_NUM(MAP_SAFARI_ZONE_NORTH) },
        { MAP_GROUP(MAP_SAFARI_ZONE_NORTHEAST),    MAP_NUM(MAP_SAFARI_ZONE_NORTHEAST) },
        { MAP_GROUP(MAP_SAFARI_ZONE_NORTHWEST),    MAP_NUM(MAP_SAFARI_ZONE_NORTHWEST) },
        { MAP_GROUP(MAP_SAFARI_ZONE_SOUTH),        MAP_NUM(MAP_SAFARI_ZONE_SOUTH) },
        { MAP_GROUP(MAP_SAFARI_ZONE_SOUTHEAST),    MAP_NUM(MAP_SAFARI_ZONE_SOUTHEAST) },
        { MAP_GROUP(MAP_SAFARI_ZONE_SOUTHWEST),    MAP_NUM(MAP_SAFARI_ZONE_SOUTHWEST) },
        { MAP_GROUP(MAP_SKY_PILLAR_TOP),           MAP_NUM(MAP_SKY_PILLAR_TOP) },
        { MAP_GROUP(MAP_SOUTHERN_ISLAND_EXTERIOR), MAP_NUM(MAP_SOUTHERN_ISLAND_EXTERIOR) },
        { MAP_GROUP(MAP_SOUTHERN_ISLAND_INTERIOR), MAP_NUM(MAP_SOUTHERN_ISLAND_INTERIOR) },
        { MAP_GROUP(MAP_RUSTBORO_CITY_GYM),        MAP_NUM(MAP_RUSTBORO_CITY_GYM) },
        { MAP_GROUP(MAP_DEWFORD_TOWN_GYM),         MAP_NUM(MAP_DEWFORD_TOWN_GYM) },
        { MAP_GROUP(MAP_MAUVILLE_CITY_GYM),        MAP_NUM(MAP_MAUVILLE_CITY_GYM) },
        { MAP_GROUP(MAP_LAVARIDGE_TOWN_GYM_1F),    MAP_NUM(MAP_LAVARIDGE_TOWN_GYM_1F) },
        { MAP_GROUP(MAP_LAVARIDGE_TOWN_GYM_B1F),   MAP_NUM(MAP_LAVARIDGE_TOWN_GYM_B1F) },
        { MAP_GROUP(MAP_PETALBURG_CITY_GYM),       MAP_NUM(MAP_PETALBURG_CITY_GYM) },
        { MAP_GROUP(MAP_FORTREE_CITY_GYM),         MAP_NUM(MAP_FORTREE_CITY_GYM) },
        { MAP_GROUP(MAP_MOSSDEEP_CITY_GYM),        MAP_NUM(MAP_MOSSDEEP_CITY_GYM) },
        { MAP_GROUP(MAP_SOOTOPOLIS_CITY_GYM_1F),   MAP_NUM(MAP_SOOTOPOLIS_CITY_GYM_1F) },
        { MAP_GROUP(MAP_SOOTOPOLIS_CITY_GYM_B1F),  MAP_NUM(MAP_SOOTOPOLIS_CITY_GYM_B1F) },
    };

    if (IsMapTypeOutdoors(mapType))
        return TRUE;

    for (i = 0; i < ARRAY_COUNT(validIndoorLocations); i++)
    {
        if (mapNum == validIndoorLocations[i].mapNum && mapGroup == validIndoorLocations[i].mapGroup)
            return TRUE;
    }

    return FALSE;
}

void FieldUseFunc_VsSeeker(u8 taskId)
{
    if (IsValidLocationForVsSeeker())
    {
        sItemUseOnFieldCB = Task_InitVsSeekerAndCheckForTrainersOnScreen;
        SetUpItemUseOnFieldCallback(taskId);
    }
    else
        DisplayDadsAdviceCannotUseItemMessage(taskId, gTasks[taskId].tUsingRegisteredKeyItem);
}

void Task_ItemUse_CloseMessageBoxAndReturnToField_VsSeeker(u8 taskId)
{
    Task_CloseCantUseKeyItemMessage(taskId);
}

static void Task_DisplayPokeFluteMessage(u8 taskId)
{
    if (WaitFanfare(FALSE))
    {
        if (!gTasks[taskId].tUsingRegisteredKeyItem)
            DisplayItemMessage(taskId, FONT_NORMAL, sText_PokeFluteAwakenedMon, CloseItemMessage);
        else
            DisplayItemMessageOnField(taskId, sText_PokeFluteAwakenedMon, Task_CloseCantUseKeyItemMessage);
    }
}

static void Task_PlayPokeFlute(u8 taskId)
{
    PlayFanfareByFanfareNum(FANFARE_RG_POKE_FLUTE);
    gTasks[taskId].func = Task_DisplayPokeFluteMessage;
}

void ItemUseOutOfBattle_PokeFlute(u8 taskId)
{
    bool32 wokeSomeoneUp = FALSE;
    u32 i;

    for (i = 0; i < CalculatePlayerPartyCount(); i++)
    {
        if (!ExecuteTableBasedItemEffect(&gParties[B_TRAINER_PLAYER][i], ITEM_AWAKENING, i, 0))
            wokeSomeoneUp = TRUE;
    }

    if (wokeSomeoneUp)
    {
        if (!gTasks[taskId].tUsingRegisteredKeyItem)
            DisplayItemMessage(taskId, FONT_NORMAL, sText_PlayedPokeFlute, Task_PlayPokeFlute);
        else
            DisplayItemMessageOnField(taskId, sText_PlayedPokeFlute, Task_PlayPokeFlute);
    }
    else
    {
        if (!gTasks[taskId].tUsingRegisteredKeyItem)
            DisplayItemMessage(taskId, FONT_NORMAL, sText_PlayedPokeFluteCatchy, CloseItemMessage);
        else
            DisplayItemMessageOnField(taskId, sText_PlayedPokeFluteCatchy, Task_CloseCantUseKeyItemMessage);
    }
}

static void ItemUseOnFieldCB_TownMap(u8 taskId)
{
    LockPlayerFieldControls();
    ScriptContext_SetupScript(EventScript_RegionMap);
    DestroyTask(taskId);
}

void ItemUseOutOfBattle_TownMap(u8 taskId)
{
    if (!gTasks[taskId].tUsingRegisteredKeyItem)
    {
        sItemUseOnFieldCB = ItemUseOnFieldCB_TownMap;
        gFieldCallback = FieldCB_UseItemOnField;
        gBagMenu->newScreenCallback = CB2_ReturnToField;
        Task_FadeAndCloseBagMenu(taskId);
    }
    else
    {
        gTasks[taskId].func = ItemUseOnFieldCB_TownMap;
    }
}

#undef tUsingRegisteredKeyItem

// ---------------------------------------------------------------------------------------
// Pokémon World: Johto's Radio key item, ported from Heart & Soul (its texts and logic).
// Music: HGSS's radio songs (Heart & Soul's, include/constants/songs_hns.h).
#include "random.h"
#include "regions.h"
#define HNS_RADIO_MUS_UNOWN      MUS_HG_RADIO_UNOWN
#define HNS_RADIO_MUS_POKE_FLUTE MUS_HG_RADIO_POKE_FLUTE
#define HNS_RADIO_MUS_ROCKET     MUS_HG_RADIO_ROCKET
#define HNS_RADIO_MUS_OAK        MUS_HG_RADIO_OAK

static const u8 sHnsText_RadioNoSignal[] = _("There's no signal here, try\nsomewhere else.{PAUSE_UNTIL_PRESS}");
static const u8 sHnsText_UnownMessage[] = _("??????????????????????????????????\l??????????????????????????????????\l??????????????????????????????????\l??????????????????????????????????{PAUSE_UNTIL_PRESS}");
static const u8 sHnsText_RadioKantoPokeFlute[] = _("POKé FLUTE RADIO.{PAUSE_UNTIL_PRESS}");
static const u8 sHnsText_OakTalk_Clefairy[] = _(
    "MARY: Tonight on POKéMON TALK,\n"
    "a rare moonlight moment!\l"
    "OAK: We're spotlighting the\l"
    "mystical CLEFAIRY!\l"
    "Trainers say they gather at\l"
    "MT. MOON under full moons.\l"
    "MARY: They DANCE in circles!\l"
    "So adorably weird!\l"
    "OAK: A timeless mystery\l"
    "and a sight to behold!\l"
    "MARY: Don't miss the chance!\l"
    "Bring a camera!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_Lapras[] = _(
    "MARY: A gentle giant takes the\n"
    "stage on today's show!\l"
    "OAK: It's the ocean ferry,\l"
    "our beloved LAPRAS!\l"
    "Seen in UNION CAVE, but only\l"
    "on Evenings. How curious!\l"
    "MARY: So rare and peaceful!\l"
    "And it sings, too!\l"
    "OAK: Some say its songs calm\l"
    "the soul of the sea.\l"
    "MARY: We love you, LAPRAS!\l"
    "Don't ever change!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_Ampharos[] = _(
    "MARY: Welcome back, everyone!\n"
    "Time for POKéMON TALK!\l"
    "OAK: Let's shine a light on\l"
    "our friend AMPHAROS!\l"
    "Its bright tail glows through\l"
    "fog to guide lost folks.\l"
    "MARY: It's powerful, elegant,\l"
    "and undeniably friendly!\l"
    "OAK: And it's a key part of\l"
    "many lighthouse stories!\l"
    "MARY: More tales soon!\l"
    "Don't touch that dial!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_Sudowoodo[] = _(
    "MARY: Up next is a real oddball\n"
    "on ROUTE 36...\l"
    "OAK: SUDOWOODO! Looks like a\l"
    "tree, but isn't one!\l"
    "It blocks the road and won't\l"
    "budge without water.\l"
    "MARY: Trainers report it reacts\l"
    "only to a SQUIRTBOTTLE!\l"
    "OAK: That's no bush it's a\l"
    "rock - type in disguise!\l"
    "MARY: Don't be fooled!\l"
    "Give it a splash!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_RedGyarados[] = _(
    "MARY: Today's story is a\n"
    "shocking one from JOHTO!\l"
    "OAK: Trainers spotted a RED\l"
    "GYARADOS at LAKE OF RAGE!\l"
    "Unlike the usual blue ones,\l"
    "this one's bright crimson!\l"
    "MARY: People say it's linked to\l"
    "some strange radio waves!\l"
    "OAK: A mysterious evolution...\l"
    "Possibly unnatural.\l"
    "MARY: Whatever the cause,\l"
    "it's making waves!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_Unown[] = _(
    "MARY: Ever visited the RUINS\n"
    "OF ALPH? Spooky stuff!\l"
    "OAK: Strange symbols line the\l"
    "walls - like ancient runes.\l"
    "Inside, you'll find UNOWN...\l"
    "each shaped like letters!\l"
    "MARY: They spell things, maybe?\l"
    "Or just freak us out!\l"
    "OAK: A true puzzle of nature,\l"
    "still unsolved to this day.\l"
    "MARY: Let us know what you\l"
    "think they mean!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_Snubbull[] = _(
    "MARY: GOLDENROD residents are\n"
    "searching high and low!\l"
    "OAK: A SNUBBULL has gone rogue\l"
    "and is on the run!\l"
    "Usually shy and fussy, it's\l"
    "been seen near the station.\l"
    "MARY: Maybe it's chasing love...\l"
    "or just adventure!\l"
    "OAK: Either way, keep your eyes\l"
    "peeled - and your leash ready.\l"
    "MARY: SNUBBULL, come home!\l"
    "We miss you!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_Slowpoke[] = _(
    "MARY: Big news from AZALEA\n"
    "TOWN this week!\l"
    "OAK: SLOWPOKE have returned to\l"
    "their well after a crisis!\l"
    "Team ROCKET had been cutting\l"
    "off their tails - horrible!\l"
    "MARY: But a brave young trainer\l"
    "shut them down!\l"
    "OAK: The SLOWPOKE are safe now\l"
    "and happily dozing again.\l"
    "MARY: Justice served and nap\l"
    "time restored!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_LavenderTower[] = _(
    "MARY: LAVENDER TOWN's tower\n"
    "has changed tunes!\l"
    "OAK: The old ghost tower is\l"
    "now a RADIO STATION!\l"
    "Some locals say it still\l"
    "feels... spooky.\l"
    "MARY: I swear I saw a GASTLY\l"
    "near the mic booth!\l"
    "OAK: Might be static... or\l"
    "specters!\l"
    "MARY: Either way, great music!\l"
    "Probably. Eep!{PAUSE_UNTIL_PRESS}");

static const u8 sHnsText_OakTalk_TentacruelWhirl[] = _(
    "MARY: Weird news from the\n"
    "WHIRL ISLANDS today!\l"
    "OAK: TENTACRUEL have been seen\l"
    "surrounding cave entrances!\l"
    "They're massive - and acting\l"
    "almost territorial.\l"
    "MARY: Trainers say they block\l"
    "the way without attacking...\l"
    "OAK: As if guarding something\l"
    "deep beneath the waves.\l"
    "MARY: What are they hiding?\l"
    "Or protecting?{PAUSE_UNTIL_PRESS}");


static const u8 sHnsText_RocketRadio[] = _("... ... Ahem, we are\nTEAM ROCKET!\lAfter three years of preparation,\lwe have risen again from the ashes!\lGIOVANNI! Can you hear us?\lWe did it!\lWhere is our boss?\lIs he listening?{PAUSE_UNTIL_PRESS}");

void ItemUseOutOfBattle_HnsRadio(u8 taskId)
{
    bool8 registered = gTasks[taskId].data[3]; // tUsingRegisteredKeyItem

    // Ruins of Alph: the Unown signal, indoors or out
    if (gMapHeader.regionMapSectionId == MAPSEC_RUINS_OF_ALPH)
    {
        DisplayCannotUseItemMessage(taskId, registered, sHnsText_UnownMessage);
        PlayBGM(HNS_RADIO_MUS_UNOWN);
        return;
    }
    if (!Overworld_MapTypeAllowsTeleportAndFly(gMapHeader.mapType))
    {
        DisplayCannotUseItemMessage(taskId, registered, sHnsText_RadioNoSignal);
        return;
    }
    if (GetCurrentRegion() == REGION_KANTO)
    {
        if (!FlagGet(FLAG_KANTO_RADIO_GOT))
        {
            DisplayCannotUseItemMessage(taskId, registered, sHnsText_RadioNoSignal);
        }
        else
        {
            DisplayCannotUseItemMessage(taskId, registered, sHnsText_RadioKantoPokeFlute);
            PlayBGM(HNS_RADIO_MUS_POKE_FLUTE);
        }
        return;
    }
    if (GetCurrentRegion() != REGION_JOHTO)
    {
        DisplayCannotUseItemMessage(taskId, registered, sHnsText_RadioNoSignal);
        return;
    }
    if (!FlagGet(FLAG_HIDE_GOLDENROD_ROCKETS))
    {
        DisplayCannotUseItemMessage(taskId, registered, sHnsText_RocketRadio);
        PlayBGM(HNS_RADIO_MUS_ROCKET);
    }
    else
    {
        static const u8 *const sOakRadioMessages[] =
        {
            sHnsText_OakTalk_Clefairy,
            sHnsText_OakTalk_Lapras,
            sHnsText_OakTalk_Ampharos,
            sHnsText_OakTalk_Sudowoodo,
            sHnsText_OakTalk_RedGyarados,
            sHnsText_OakTalk_Unown,
            sHnsText_OakTalk_Snubbull,
            sHnsText_OakTalk_Slowpoke,
            sHnsText_OakTalk_LavenderTower,
            sHnsText_OakTalk_TentacruelWhirl,
        };
        DisplayCannotUseItemMessage(taskId, registered, sOakRadioMessages[Random() % ARRAY_COUNT(sOakRadioMessages)]);
        PlayBGM(HNS_RADIO_MUS_OAK);
    }
}
