// Pokémon World: stand-ins for Heart & Soul's script specials and natives that the
// Johto and later-Kanto maps call but that this game doesn't have yet. They let the
// scripts run without doing the real thing. Each is a TODO for a later stage.
// (Heart & Soul's randomizer, nuzlocke and challenge helpers are not brought over at
// all; tools/pokemon_world/import_hns.py takes those calls out of the scripts.)
#include "global.h"
#include "event_data.h"
#include "script.h"
#include "constants/items.h"
#include "constants/pokemon.h"

// --- specials (data/specials.inc) ---------------------------------------------------

// Legendary / fossil / gift checks: "not there" for now.
u16 HnsStub_CheckAerodactyl(void) { return gSpecialVar_Result = FALSE; }
u16 HnsStub_CheckCelebi(void)     { return gSpecialVar_Result = FALSE; }
u16 HnsStub_CheckHooh(void)       { return gSpecialVar_Result = FALSE; }
u16 HnsStub_CheckKabuto(void)     { return gSpecialVar_Result = FALSE; }
u16 HnsStub_CheckOmanyte(void)    { return gSpecialVar_Result = FALSE; }
u16 HnsStub_CheckTogepi(void)     { return gSpecialVar_Result = FALSE; }

// Ruins of Alph sliding puzzles: solved straight away for now.
u16 HnsStub_DoSlidingPuzzle(void) { return gSpecialVar_Result = TRUE; }

// Bug-Catching Contest (National Park).
u16 HnsStub_EnterBugContestMode(void)      { return 0; }
u16 HnsStub_ShowBugContestChosenMon(void)  { return 0; }

// Move tutors, Day-Care, party size, haircut brothers, rival naming, abilities, shinies.
u16 HnsStub_GetBattleFrontierTutorMoveIndex(void) { return gSpecialVar_Result = 0; }
u16 HnsStub_GetDaycareCost(void)  { return gSpecialVar_0x8005 = 0; }
u16 HnsStub_GetMaxPartySize(void) { return gSpecialVar_Result = PARTY_SIZE; }
u16 HnsStub_HaircutBrother1(void) { return 0; }
u16 HnsStub_NameRival(void)       { return 0; }
u16 HnsStub_SwitchMonAbility(void) { return 0; }
u16 HnsStub_ToggleShinyColors(void) { return 0; }

// --- natives (callnative) -------------------------------------------------------------

// Wild encounters by time of day: a later stage brings Johto's wild Pokémon.
void HnsStub_SetTimeBasedEncounters(struct ScriptContext *ctx) { }

// Heart & Soul's own script commands (asm/macros/hns_compat.inc).
void HnsNative_GiveNamedMon(struct ScriptContext *ctx)    { ScriptReadHalfword(ctx); gSpecialVar_Result = MON_CANT_GIVE; }
void HnsNative_RemoveNamedMon(struct ScriptContext *ctx)  { ScriptReadHalfword(ctx); gSpecialVar_Result = FALSE; }
void HnsNative_RemoveGenericMon(struct ScriptContext *ctx) { ScriptReadHalfword(ctx); gSpecialVar_Result = FALSE; }
void HnsNative_Remove5Mons(struct ScriptContext *ctx)     { }
void HnsNative_BaobaCheckMon(struct ScriptContext *ctx)   { ScriptReadHalfword(ctx); gSpecialVar_Result = FALSE; }
void HnsNative_GiveOddEgg(struct ScriptContext *ctx)      { ScriptReadHalfword(ctx); gSpecialVar_Result = MON_CANT_GIVE; }
void HnsNative_GiveBP(struct ScriptContext *ctx)          { ScriptReadHalfword(ctx); }
void HnsNative_BufferMonCategory(struct ScriptContext *ctx) { ScriptReadByte(ctx); ScriptReadHalfword(ctx); }
void HnsNative_CheckPartyMonLevel(struct ScriptContext *ctx) { ScriptReadHalfword(ctx); gSpecialVar_Result = FALSE; }
void HnsNative_ChooseItem(struct ScriptContext *ctx)      { ScriptReadHalfword(ctx); VarSet(VAR_ITEM_ID, ITEM_NONE); }
