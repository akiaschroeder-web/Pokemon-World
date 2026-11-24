// Pokémon World: Heart & Soul's script specials and script commands for its Johto and
// later-Kanto maps, ported to pokeemerald-expansion (Heart & Soul is credited in
// PROJECT_CREDITS.md). tools/pokemon_world/import_hns.py renames Heart & Soul's special X to
// Hns_X (HNS_SPECIALS there); its own script commands are natives (asm/macros/hns_compat.inc).
// Heart & Soul's randomizer / nuzlocke / challenge options are not brought over, so the parts
// of these that only served them are left out (noted where it matters).
#include "global.h"
#include "battle.h"
#include "bug_contest_hns.h"
#include "daycare.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_screen_effect.h"
#include "item.h"
#include "mail.h"
#include "main.h"
#include "move.h"
#include "naming_screen.h"
#include "party_menu.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "string_util.h"
#include "constants/battle_frontier.h"
#include "constants/easy_chat.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"

// --- legendary / fossil / gift checks (Heart & Soul: braille_puzzles.c) -----------------
// The lead Pokémon must be the right one.

static enum Species LeadSpecies(void)
{
    return GetMonData(&gPlayerParty[0], MON_DATA_SPECIES_OR_EGG);
}

u16 Hns_CheckHooh(void)       { return gSpecialVar_Result = (LeadSpecies() == SPECIES_HO_OH); }
u16 Hns_CheckAerodactyl(void) { return gSpecialVar_Result = (LeadSpecies() == SPECIES_AERODACTYL); }
u16 Hns_CheckKabuto(void)     { return gSpecialVar_Result = (LeadSpecies() == SPECIES_KABUTO); }
u16 Hns_CheckOmanyte(void)    { return gSpecialVar_Result = (LeadSpecies() == SPECIES_OMANYTE); }

u16 Hns_CheckTogepi(void)
{
    enum Species s = LeadSpecies();
    return gSpecialVar_Result = (s == SPECIES_TOGEPI || s == SPECIES_TOGETIC || s == SPECIES_TOGEKISS);
}

// Ilex Forest shrine: Celebi in the lead, at full HP, and walking behind the player.
u16 Hns_CheckCelebi(void)
{
    struct Pokemon *mon = &gPlayerParty[0];
    struct ObjectEvent *follower;

    gSpecialVar_Result = FALSE;
    if (GetMonData(mon, MON_DATA_SPECIES_OR_EGG) != SPECIES_CELEBI)
        return FALSE;
    if (GetMonData(mon, MON_DATA_HP) != GetMonData(mon, MON_DATA_MAX_HP))
        return FALSE;
    follower = GetFollowerObject();
    if (follower == NULL || follower->invisible)
        return FALSE;
    return gSpecialVar_Result = TRUE;
}

// --- party, Day-Care, grooming, abilities -----------------------------------------------

// Heart & Soul's party-limit challenge isn't brought over: always a full party.
u16 Hns_GetMaxPartySize(void)
{
    return gSpecialVar_Result = PARTY_SIZE;
}

// Heart & Soul's GetDaycareCost is Emerald's; expansion calls it GetDaycareCostAndPrepareString.
void Hns_GetDaycareCost(void)
{
    GetDaycareCostAndPrepareString();
}

// Haircut brothers / Daisy: Heart & Soul's FRIENDSHIP_EVENT_HAIRCUT1 (+99 at any friendship).
void Hns_HaircutBrother1(void)
{
    struct Pokemon *mon = &gPlayerParty[gSpecialVar_0x8004];
    s32 friendship;

    if (gSpecialVar_0x8004 >= PARTY_SIZE || GetMonData(mon, MON_DATA_IS_EGG))
        return;
    friendship = GetMonData(mon, MON_DATA_FRIENDSHIP) + 99;
    if (friendship > MAX_FRIENDSHIP)
        friendship = MAX_FRIENDSHIP;
    SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
}

// Switches the Pokémon in VAR_0x8004 between its two normal abilities (VAR_RESULT = done).
void Hns_SwitchMonAbility(void)
{
    struct Pokemon *mon = &gPlayerParty[gSpecialVar_0x8004];
    enum Species species = GetMonData(mon, MON_DATA_SPECIES);
    u8 abilityNum = GetMonData(mon, MON_DATA_ABILITY_NUM);

    if (abilityNum <= 1 && GetSpeciesAbility(species, 1) != ABILITY_NONE
     && GetSpeciesAbility(species, 0) != GetSpeciesAbility(species, 1))
    {
        abilityNum = !abilityNum;
        SetMonData(mon, MON_DATA_ABILITY_NUM, &abilityNum);
        gSpecialVar_Result = TRUE;
    }
    else
    {
        gSpecialVar_Result = FALSE;
    }
}

// Heart & Soul's IV / EV services: set the Pokémon in VAR_0x8004's stat to the value in the
// var Heart & Soul's scripts use for it.
static void SetPartyMonStat(u32 field, u16 var)
{
    u8 value = VarGet(var);
    if (gSpecialVar_0x8004 >= PARTY_SIZE)
        return;
    SetMonData(&gPlayerParty[gSpecialVar_0x8004], field, &value);
    CalculateMonStats(&gPlayerParty[gSpecialVar_0x8004]);
}

void Hns_SetHpIvs(void)    { SetPartyMonStat(MON_DATA_HP_IV, VAR_0x8007); }
void Hns_SetAtkIvs(void)   { SetPartyMonStat(MON_DATA_ATK_IV, VAR_0x8008); }
void Hns_SetDefIvs(void)   { SetPartyMonStat(MON_DATA_DEF_IV, VAR_0x8009); }
void Hns_SetSpdIvs(void)   { SetPartyMonStat(MON_DATA_SPEED_IV, VAR_0x800A); }
void Hns_SetSpAtkIvs(void) { SetPartyMonStat(MON_DATA_SPATK_IV, VAR_0x800B); }
void Hns_SetSpDefIvs(void) { SetPartyMonStat(MON_DATA_SPDEF_IV, VAR_UNUSED_0x8014); }
void Hns_SetHpEvs(void)    { SetPartyMonStat(MON_DATA_HP_EV, VAR_0x8000); }
void Hns_SetAtkEvs(void)   { SetPartyMonStat(MON_DATA_ATK_EV, VAR_0x8001); }
void Hns_SetDefEvs(void)   { SetPartyMonStat(MON_DATA_DEF_EV, VAR_0x8002); }
void Hns_SetSpdEvs(void)   { SetPartyMonStat(MON_DATA_SPEED_EV, VAR_0x8003); }
void Hns_SetSpAtkEvs(void) { SetPartyMonStat(MON_DATA_SPATK_EV, VAR_0x8005); }
void Hns_SetSpDefEvs(void) { SetPartyMonStat(MON_DATA_SPDEF_EV, VAR_0x8006); }

// --- Trainer Hill courtyard move tutors (Heart & Soul's three Battle Frontier-style lists) ---
// Heart & Soul's scripts pass the list (VAR_TEMP_FRONTIER_TUTOR_ID, also VAR_0x8005 for the
// name) and the choice (VAR_TEMP_FRONTIER_TUTOR_SELECTION, also VAR_0x8004); expansion's
// BufferBattleFrontierTutorMoveName / ChooseMonForMoveTutor take the move in VAR_0x8005.
static const u16 sHnsTutorMoves[3][10] =
{
    { MOVE_SOFT_BOILED, MOVE_SEISMIC_TOSS, MOVE_DREAM_EATER, MOVE_MEGA_PUNCH, MOVE_MEGA_KICK,
      MOVE_BODY_SLAM, MOVE_ROCK_SLIDE, MOVE_COUNTER, MOVE_THUNDER_WAVE, MOVE_SWORDS_DANCE },
    { MOVE_DEFENSE_CURL, MOVE_SNORE, MOVE_MUD_SLAP, MOVE_SWIFT, MOVE_ICY_WIND,
      MOVE_ENDURE, MOVE_PSYCH_UP, MOVE_ICE_PUNCH, MOVE_THUNDER_PUNCH, MOVE_FIRE_PUNCH },
    { MOVE_FRENZY_PLANT, MOVE_BLAST_BURN, MOVE_HYDRO_CANNON },
};

static u16 HnsTutorMove(u16 tutor, u16 selection)
{
    if (tutor >= ARRAY_COUNT(sHnsTutorMoves) || selection >= ARRAY_COUNT(sHnsTutorMoves[0]))
        return MOVE_NONE;
    return sHnsTutorMoves[tutor][selection];
}

void Hns_BufferBattleFrontierTutorMoveName(void)
{
    StringCopy(gStringVar1, GetMoveName(HnsTutorMove(gSpecialVar_0x8005, gSpecialVar_0x8004)));
}

void Hns_GetBattleFrontierTutorMoveIndex(void)
{
    gSpecialVar_0x8005 = HnsTutorMove(VarGet(VAR_TEMP_FRONTIER_TUTOR_ID), VarGet(VAR_TEMP_FRONTIER_TUTOR_SELECTION));
}

// --- Johto's rival ------------------------------------------------------------------------
// Heart & Soul names Silver here (the police ask in Elm's lab; the Goldenrod name-changer
// asks again). Kept apart from the Hoenn/Sinnoh rival: see GetJohtoRivalName.
void Hns_NameRival(void)
{
    DoNamingScreen(NAMING_SCREEN_JOHTO_RIVAL, gSaveBlock3Ptr->johtoRivalName, 0, 0, 0, CB2_ReturnToFieldContinueScript);
}

// --- Heart & Soul's own script commands (natives; asm/macros/hns_compat.inc) ---------------

static u32 PlayerOtId(void)
{
    return gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8)
         | (gSaveBlock2Ptr->playerTrainerId[2] << 16) | (gSaveBlock2Ptr->playerTrainerId[3] << 24);
}

static void SetPokedexCaught(struct Pokemon *mon)
{
    HandleSetPokedexFlagFromMon(mon, FLAG_SET_SEEN);
    HandleSetPokedexFlagFromMon(mon, FLAG_SET_CAUGHT);
}

// givenamedmon: 1 = Kenya (Spearow with mail), 2 = Shuckie, 3 = Bill's Eevee, 4 = Dragon's
// Den Dratini (shiny is Heart & Soul's prize for the Elder's quiz; Adamant, ExtremeSpeed).
void HnsNative_GiveNamedMon(struct ScriptContext *ctx)
{
    static const u8 sKenyaNickname[] = _("KENYA");
    static const u8 sKenyaOtName[] = _("RUDY");
    static const u8 sShuckieNickname[] = _("SHUCKIE");
    static const u8 sShuckieOtName[] = _("KIRK");
    static const u8 sEeveeOtName[] = _("BILL");
    static const u16 sKenyaMailWords[MAIL_WORDS_COUNT] =
    {
        EC_WORD_YUP, EC_WORD_MAIL, EC_WORD_TIME, EC_WORD_TAKE, EC_WORD_THIS,
        EC_WORD_POKEMON, EC_WORD_DON_T, EC_WORD_LOSE, EC_WORD_IT,
    };
    u16 giftId = ScriptReadHalfword(ctx);
    struct Pokemon mon;
    enum Species species;
    u8 level;
    enum Item item = ITEM_NONE;
    const u8 *nickname = NULL, *otName = NULL;
    u32 otId = 0;

    switch (giftId)
    {
    case 1: species = SPECIES_SPEAROW; level = 20; item = ITEM_RETRO_MAIL; nickname = sKenyaNickname; otName = sKenyaOtName; otId = 61225; break;
    case 2: species = SPECIES_SHUCKLE; level = 20; item = ITEM_BERRY_JUICE; nickname = sShuckieNickname; otName = sShuckieOtName; otId = 4336; break;
    case 3: species = SPECIES_EEVEE;   level = 20; otName = sEeveeOtName; otId = 5231; break;
    case 4: species = SPECIES_DRATINI; level = 15; break;
    default:
        gSpecialVar_Result = MON_CANT_GIVE;
        return;
    }
    if (CalculatePlayerPartyCount() >= PARTY_SIZE)
    {
        gSpecialVar_Result = MON_CANT_GIVE;
        return;
    }

    if (giftId == 4)
    {
        CreateRandomMon(&mon, species, level);
        u8 nature = NATURE_ADAMANT;
        SetMonData(&mon, MON_DATA_HIDDEN_NATURE, &nature);
        bool32 shiny = TRUE;
        enum Move move = MOVE_EXTREME_SPEED;
        u8 pp = GetMovePP(move);
        SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
        SetMonData(&mon, MON_DATA_MOVE1, &move);
        SetMonData(&mon, MON_DATA_PP1, &pp);
    }
    else
    {
        CreateRandomMon(&mon, species, level);
        SetMonData(&mon, MON_DATA_OT_ID, &otId);
    }
    if (nickname != NULL)
        SetMonData(&mon, MON_DATA_NICKNAME, nickname);
    if (otName != NULL)
        SetMonData(&mon, MON_DATA_OT_NAME, otName);
    if (item == ITEM_RETRO_MAIL)
    {
        struct Mail mail = {0};
        memcpy(mail.words, sKenyaMailWords, sizeof(sKenyaMailWords));
        StringCopy(mail.playerName, sKenyaOtName);
        mail.trainerId[0] = otId;
        mail.trainerId[1] = otId >> 8;
        mail.trainerId[2] = otId >> 16;
        mail.trainerId[3] = otId >> 24;
        mail.species = species;
        mail.itemId = item;
        GiveMailToMon(&mon, &mail);
    }
    else if (item != ITEM_NONE)
    {
        SetMonData(&mon, MON_DATA_HELD_ITEM, &item);
    }
    CalculateMonStats(&mon);
    SetPokedexCaught(&mon);
    gSpecialVar_Result = GiveCapturedMonToPlayer(&mon);
}

static bool32 PartyMonHasNickname(struct Pokemon *mon, const u8 *name)
{
    u8 nickname[POKEMON_NAME_BUFFER_SIZE];
    GetMonData(mon, MON_DATA_NICKNAME, nickname);
    return StringCompare(nickname, name) == 0;
}

// removenamedmon: takes Kenya (must still hold the mail) or Shuckie back.
// VAR_RESULT: MON_GIVEN_TO_PARTY = taken, 3 = Shuckie is too attached (friendship > 200).
void HnsNative_RemoveNamedMon(struct ScriptContext *ctx)
{
    static const u8 sKenyaNickname[] = _("KENYA");
    static const u8 sShuckieNickname[] = _("SHUCKIE");
    u16 giftId = ScriptReadHalfword(ctx);
    const u8 *name = giftId == 1 ? sKenyaNickname : giftId == 2 ? sShuckieNickname : NULL;
    u32 i;

    gSpecialVar_Result = MON_CANT_GIVE;
    if (name == NULL || CalculatePlayerPartyCount() <= 1)
        return;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE || !PartyMonHasNickname(mon, name))
            continue;
        if (giftId == 1 && !ItemIsMail(GetMonData(mon, MON_DATA_HELD_ITEM)))
            return;
        if (giftId == 2 && GetMonData(mon, MON_DATA_FRIENDSHIP) > 200)
        {
            gSpecialVar_Result = 3;
            return;
        }
        ZeroMonData(mon);
        CompactPartySlots();
        CalculatePlayerPartyCount();
        gSpecialVar_Result = MON_GIVEN_TO_PARTY;
        return;
    }
}

// removegenericmon: takes the Pokémon in party slot VAR_0x8004 if it is that species
// (Heart & Soul's values: MON_SATISFACTORY for a level-100 Magikarp, else MON_UNSATISFACTORY).
#define HNS_MON_UNSATISFACTORY 1
#define HNS_MON_SATISFACTORY   2
void HnsNative_RemoveGenericMon(struct ScriptContext *ctx)
{
    enum Species target = ScriptReadHalfword(ctx);
    struct Pokemon *mon;
    enum Species species;

    gSpecialVar_Result = FALSE;
    if (gSpecialVar_0x8004 >= PARTY_SIZE)
        return;
    mon = &gPlayerParty[gSpecialVar_0x8004];
    species = GetMonData(mon, MON_DATA_SPECIES);
    if (species == SPECIES_NONE || species != target)
        return;
    gSpecialVar_Result = (species == SPECIES_MAGIKARP && GetMonData(mon, MON_DATA_LEVEL) == 100) ? HNS_MON_SATISFACTORY : HNS_MON_UNSATISFACTORY;
    ZeroMonData(mon);
    CompactPartySlots();
    CalculatePlayerPartyCount();
}

// remove5mons: Bug-Catching Contest entry keeps only the lead (the party is saved first).
void HnsNative_Remove5Mons(struct ScriptContext *ctx)
{
    u32 i;

    if (GetMonData(&gPlayerParty[0], MON_DATA_HP) == 0 || GetMonData(&gPlayerParty[0], MON_DATA_SPECIES_OR_EGG) == SPECIES_EGG)
    {
        gSpecialVar_Result = MON_CANT_GIVE;
        return;
    }
    for (i = 1; i < PARTY_SIZE; i++)
        ZeroMonData(&gPlayerParty[i]);
    CalculatePlayerPartyCount();
    gSpecialVar_Result = MON_GIVEN_TO_PARTY;
}

// baobacheckmon: Baoba's Safari Zone requests (is party slot VAR_0x8004 on list 1-4?).
void HnsNative_BaobaCheckMon(struct ScriptContext *ctx)
{
    static const u16 sLists[4][9] =
    {
        { SPECIES_CACNEA, SPECIES_LOTAD, SPECIES_MAKUHITA, SPECIES_LOMBRE, SPECIES_TRAPINCH, SPECIES_BELDUM, SPECIES_VIBRAVA },
        { SPECIES_TROPIUS, SPECIES_CHIMECHO, SPECIES_ABSOL, SPECIES_CASTFORM },
        { SPECIES_BARBOACH, SPECIES_WHISCASH, SPECIES_MEDITITE, SPECIES_NUMEL, SPECIES_BALTOY, SPECIES_ABSOL, SPECIES_MEDICHAM, SPECIES_CAMERUPT },
        { SPECIES_WHISMUR, SPECIES_NOSEPASS, SPECIES_BAGON, SPECIES_RELICANTH, SPECIES_FEEBAS },
    };
    u16 list = ScriptReadHalfword(ctx);
    struct Pokemon *mon;
    enum Species species;
    u32 i;

    gSpecialVar_Result = FALSE;
    if (list < 1 || list > ARRAY_COUNT(sLists) || gSpecialVar_0x8004 >= PARTY_SIZE)
        return;
    mon = &gPlayerParty[gSpecialVar_0x8004];
    species = GetMonData(mon, MON_DATA_SPECIES);
    if (species == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG))
        return;
    for (i = 0; i < ARRAY_COUNT(sLists[0]); i++)
        if (sLists[list - 1][i] != SPECIES_NONE && sLists[list - 1][i] == species)
            gSpecialVar_Result = TRUE;
}

// giveoddegg 1-7: the Day-Care man's Odd Egg (14% shiny, knows Dizzy Punch, close to hatching).
// (Heart & Soul's list of player names that always get a shiny egg is not brought over.)
void HnsNative_GiveOddEgg(struct ScriptContext *ctx)
{
    static const u16 sOddEggSpecies[] =
    {
        SPECIES_NONE, SPECIES_PICHU, SPECIES_CLEFFA, SPECIES_IGGLYBUFF, SPECIES_TYROGUE,
        SPECIES_SMOOCHUM, SPECIES_ELEKID, SPECIES_MAGBY,
    };
    u16 which = VarGet(ScriptReadHalfword(ctx));
    struct Pokemon mon;
    bool32 shiny;
    u8 cycles;

    if (which == 0 || which >= ARRAY_COUNT(sOddEggSpecies) || CalculatePlayerPartyCount() >= PARTY_SIZE)
    {
        gSpecialVar_Result = MON_CANT_GIVE;
        return;
    }
    CreateEgg(&mon, sOddEggSpecies[which], TRUE);
    shiny = (Random() % 100) < 14;
    SetMonData(&mon, MON_DATA_IS_SHINY, &shiny);
    cycles = gSpeciesInfo[sOddEggSpecies[which]].eggCycles;
    if (cycles > 2)
        cycles = 2;
    SetMonData(&mon, MON_DATA_FRIENDSHIP, &cycles);
    SetMonMoveSlot(&mon, MOVE_DIZZY_PUNCH, 1);
    gSpecialVar_Result = GiveCapturedMonToPlayer(&mon);
}

// givebp: Battle Points (amount in STR_VAR_1 for the message).
void HnsNative_GiveBP(struct ScriptContext *ctx)
{
    u16 add = VarGet(ScriptReadHalfword(ctx));
    u32 total = gSaveBlock2Ptr->frontier.battlePoints + add;

    gSaveBlock2Ptr->frontier.battlePoints = min(total, MAX_BATTLE_FRONTIER_POINTS);
    total = gSaveBlock2Ptr->frontier.cardBattlePoints + add;
    gSaveBlock2Ptr->frontier.cardBattlePoints = min(total, 0xFFFF);
    ConvertIntToDecimalStringN(gStringVar1, add, STR_CONV_MODE_LEFT_ALIGN, 3);
}

// buffermoncategory: the species' Pokédex category ("Leaf Pokémon"...) for Elm's starter
// question; VAR_TEMP_2 = the species, as in Heart & Soul.
void HnsNative_BufferMonCategory(struct ScriptContext *ctx)
{
    u8 stringVarIndex = ScriptReadByte(ctx);
    enum Species species = VarGet(ScriptReadHalfword(ctx));

    VarSet(VAR_TEMP_2, species);
    if (stringVarIndex < 3)
        StringCopy(stringVarIndex == 0 ? gStringVar1 : stringVarIndex == 1 ? gStringVar2 : gStringVar3, GetSpeciesCategory(species));
}

// checkpartymonlevel: is party slot VAR_0x8004 at level 100? (Heart & Soul ignores the argument.)
void HnsNative_CheckPartyMonLevel(struct ScriptContext *ctx)
{
    ScriptReadHalfword(ctx);
    gSpecialVar_Result = gSpecialVar_0x8004 < PARTY_SIZE && GetMonData(&gPlayerParty[gSpecialVar_0x8004], MON_DATA_LEVEL) == 100;
}

// setwildbattleshiny: like setwildbattle, but the Pokémon is shiny (the red Gyarados).
void HnsNative_SetWildBattleShiny(struct ScriptContext *ctx)
{
    enum Species species = ScriptReadHalfword(ctx);
    u8 level = ScriptReadByte(ctx);
    enum Item item = ScriptReadHalfword(ctx);
    bool32 shiny = TRUE;

    CreateScriptedWildMon(species, level, item);
    SetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY, &shiny);
}

// Heart & Soul's checkpartymove for a move that isn't one of expansion's field moves (the
// Headbutt trees): a Pokémon that knows it, or else one that could learn it (Heart & Soul lets
// tutor moves count, since its Headbutt is taught by a tutor). VAR_RESULT = slot or PARTY_SIZE.
void HnsNative_CheckPartyMove(struct ScriptContext *ctx)
{
    enum Move move = ScriptReadHalfword(ctx);
    u32 i, pass;

    gSpecialVar_Result = PARTY_SIZE;
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < PARTY_SIZE; i++)
        {
            struct Pokemon *mon = &gPlayerParty[i];
            enum Species species = GetMonData(mon, MON_DATA_SPECIES);
            if (species == SPECIES_NONE)
                break;
            if (GetMonData(mon, MON_DATA_IS_EGG))
                continue;
            if (pass == 0 ? MonKnowsMove(mon, move) : CanLearnTeachableMove(species, move))
            {
                gSpecialVar_Result = i;
                gSpecialVar_0x8004 = species;
                return;
            }
        }
    }
}
