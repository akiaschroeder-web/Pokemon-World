// Pokémon World: National Park's Bug-Catching Contest, ported from Heart & Soul's
// bug_contest.c (credited in PROJECT_CREDITS.md). Its scripts are Heart & Soul's
// (data/scripts/hns_shared.inc, data/maps/*NationalPark*_Hns). Engine hooks:
//   field_control_avatar.c  the time limit, checked every step
//   battle_setup.c          wild battles end through Hns_CB2_EndBugContestBattle
//   battle_script_commands.c no Exp. during the contest
//   start_menu.c            "Retire" (the Safari Zone's start menu)
//   overworld.c             CB2_HnsBugContestWhiteOut
// Heart & Soul clears/sets its nuzlocke "adventure started" flag here; that's left out.
#include "global.h"
#include "battle.h"
#include "bug_contest_hns.h"
#include "event_data.h"
#include "field_screen_effect.h"
#include "item.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "script.h"
#include "sprite.h"
#include "string_util.h"
#include "constants/battle.h"
#include "constants/items.h"
#include "constants/species.h"

static EWRAM_DATA u32 sBugContestStartTime = 0;
static EWRAM_DATA bool8 sBugContestTimerActive = FALSE;

bool32 Hns_GetBugContestFlag(void)
{
    return FlagGet(FLAG_SYS_BUG_CONTEST_MODE);
}

void Hns_BugContestRetirePrompt(void)
{
    ScriptContext_SetupScript(BugContest_EventScript_TimesUp_Hns);
}

void Hns_EnterBugContestMode(void)
{
    FlagSet(FLAG_SYS_BUG_CONTEST_MODE);
    sBugContestStartTime = gMain.vblankCounter1;
    sBugContestTimerActive = TRUE;
}

void Hns_ExitBugContestMode(void)
{
    FlagClear(FLAG_SYS_BUG_CONTEST_MODE);
    sBugContestTimerActive = FALSE;
}

// Called every step. (After loading a save mid-contest the timer starts again, as in Heart & Soul.)
bool32 Hns_BugContestCheckTimeLimit(void)
{
    if (!FlagGet(FLAG_SYS_BUG_CONTEST_MODE))
        return FALSE;
    if (!sBugContestTimerActive)
    {
        sBugContestStartTime = gMain.vblankCounter1;
        sBugContestTimerActive = TRUE;
    }
    if (gMain.vblankCounter1 - sBugContestStartTime >= HNS_BUG_CONTEST_TIME_LIMIT_FRAMES)
    {
        sBugContestTimerActive = FALSE;
        ScriptContext_SetupScript(BugContest_EventScript_TimesUp_Hns);
        return TRUE;
    }
    return FALSE;
}

// The caught Pokémon chosen for judging (party slot VAR_0x8004) goes to the PC.
void Hns_TransferBugContestMon(void)
{
    struct Pokemon *mon = &gPlayerParty[VarGet(VAR_0x8004) % PARTY_SIZE];

    if (CopyMonToPC(mon) != MON_CANT_GIVE)
    {
        ZeroMonData(mon);
        CompactPartySlots();
        CalculatePlayerPartyCount();
        gSpecialVar_Result = MON_GIVEN_TO_PC;
    }
    else
    {
        gSpecialVar_Result = MON_CANT_GIVE;
    }
}

// Heart & Soul's judging: by the Pokémon's max HP; VAR_RESULT = place (1-3), VAR_0x8005 = prize.
void Hns_JudgeBugContestMon(void)
{
    static const u16 sFirstPlaceRewards[]  = { ITEM_MOON_STONE, ITEM_SUN_STONE, ITEM_LEAF_STONE };
    static const u16 sSecondPlaceRewards[] = { ITEM_FIRE_STONE, ITEM_THUNDER_STONE, ITEM_WATER_STONE };
    static const u16 sThirdPlaceRewards[]  =
    {
        ITEM_ORAN_BERRY, ITEM_CHERI_BERRY, ITEM_PERSIM_BERRY, ITEM_PECHA_BERRY,
        ITEM_RAWST_BERRY, ITEM_ASPEAR_BERRY, ITEM_CHESTO_BERRY,
    };
    u16 maxHP = GetMonData(&gPlayerParty[VarGet(VAR_0x8004) % PARTY_SIZE], MON_DATA_MAX_HP);
    u16 rand = Random() % 100;

    if (maxHP < 41)
        gSpecialVar_Result = 3;
    else if (maxHP <= 46)
        gSpecialVar_Result = (rand < 50) ? 2 : 3;
    else if (maxHP <= 47)
        gSpecialVar_Result = (rand < 75) ? 1 : 2;
    else
        gSpecialVar_Result = 1;

    switch (gSpecialVar_Result)
    {
    case 1:  VarSet(VAR_0x8005, sFirstPlaceRewards[Random() % ARRAY_COUNT(sFirstPlaceRewards)]); break;
    case 2:  VarSet(VAR_0x8005, sSecondPlaceRewards[Random() % ARRAY_COUNT(sSecondPlaceRewards)]); break;
    default: VarSet(VAR_0x8005, sThirdPlaceRewards[Random() % ARRAY_COUNT(sThirdPlaceRewards)]); break;
    }
}

void Hns_CB2_EndBugContestBattle(void)
{
    CpuFill16(0, (void *)(BG_PLTT), BG_PLTT_SIZE);
    ResetOamRange(0, 128);

    if (gBattleOutcome == B_OUTCOME_LOST || gBattleOutcome == B_OUTCOME_DREW)
    {
        SetMainCallback2(CB2_HnsBugContestWhiteOut);
        return;
    }
    if (gBattleOutcome == B_OUTCOME_CAUGHT && CalculatePlayerPartyCount() == PARTY_SIZE)
        ScriptContext_SetupScript(BugContest_EventScript_TimesUp_Hns);
    SetMainCallback2(CB2_ReturnToField);
    gFieldCallback = FieldCB_ReturnToFieldNoScriptCheckMusic;
}

// The contest's Park Balls are Safari Balls in Heart & Soul; take any left over.
void Hns_RemoveSafariBalls(void)
{
    u16 count = CountTotalItemQuantityInBag(ITEM_SAFARI_BALL);
    if (count > 0)
        RemoveBagItem(ITEM_SAFARI_BALL, count);
}

// The judging scene: VAR_RESULT = local id of the map's object showing the chosen bug,
// STR_VAR_1 = its species name.
void Hns_ShowBugContestChosenMon(void)
{
    enum Species species = GetMonData(&gPlayerParty[VarGet(VAR_0x8004) % PARTY_SIZE], MON_DATA_SPECIES);

    StringCopy(gStringVar1, GetSpeciesName(species));
    switch (species)
    {
    case SPECIES_CATERPIE:   gSpecialVar_Result = 22; break;
    case SPECIES_WEEDLE:     gSpecialVar_Result = 23; break;
    case SPECIES_METAPOD:    gSpecialVar_Result = 24; break;
    case SPECIES_KAKUNA:     gSpecialVar_Result = 25; break;
    case SPECIES_PARAS:      gSpecialVar_Result = 26; break;
    case SPECIES_VENONAT:    gSpecialVar_Result = 27; break;
    case SPECIES_BUTTERFREE: gSpecialVar_Result = 28; break;
    case SPECIES_BEEDRILL:   gSpecialVar_Result = 29; break;
    case SPECIES_SCYTHER:    gSpecialVar_Result = 30; break;
    case SPECIES_PINSIR:     gSpecialVar_Result = 31; break;
    default:                 gSpecialVar_Result = 0; break;
    }
}
