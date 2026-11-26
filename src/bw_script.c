// Pokémon World: runs Pokémon Black's own event scripts in Unova.
//
// BW's scripts (converted from the owner's ROM by tools/pokemon_world/import_bw.py, see
// bw/vmdata.py) are bytecode for Game Freak's Gen 5 script engine. This interpreter executes
// that bytecode with our engine: messages and yes/no boxes, NPC movement, story flags and
// variables, items, Pokémon, battles, warps, fades and sound. Commands we don't do anything with
// yet (camera moves, special effects, link/online features) are read and skipped, so a script
// always gets to its end.
//
// Our scripts start it with `bwscript <zone>, <script id>` (asm/macros/event.inc): the NPC, sign and
// trigger scripts the converter writes do just that. The interpreter then runs as the script
// context's native function until BW's script ends.
//
// BW's script format (bw/scripts.py): a table of s32 offsets ended by 0xFD13; script id n is entry
// n-1. An instruction is a u16 opcode then its parameters, unpadded (parameter sizes from
// PokeScriptSDK5's BW command list, here as sBwParamSizes). Global script ids (2000+) are looked
// up in BW's range table (sBwGlobalRanges): each range has its own script and text file.
//
// Story state: BW's flags are FLAG_UNOVA_START + n, its saved work variables 0x4000+n are
// VAR_UNOVA_START + n (both in SaveBlock1's Unova block); its temporary work variables
// (0x8000+) live here.

#include "global.h"
#include "bw_script.h"
#include "battle.h"
#include "battle_setup.h"
#include "event_data.h"
#include "event_object_lock.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_message_box.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "item.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon_world.h"
#include "shop.h"
#include "starter_choose.h"
#include "random.h"
#include "rtc.h"
#include "regions.h"
#include "script.h"
#include "script_menu.h"
#include "script_movement.h"
#include "script_pokemon_util.h"
#include "sound.h"
#include "string_util.h"
#include "trainer_see.h"
#include "constants/battle.h"
#include "constants/event_objects.h"
#include "constants/opponents.h"
#include "constants/flags.h"
#include "constants/rgb.h"
#include "constants/items.h"
#include "constants/map_groups.h"
#include "constants/songs.h"
#include "constants/vars.h"

struct BwScriptFile { const u8 *data; u32 size; };
struct BwTextFile { const u8 *const *strings; const u32 *count; };
struct BwGlobalRange { u16 first, last, file, text; };
struct BwZone { u16 scripts, text; };
struct BwZoneMap { u16 zone, map, x, y, w, h; };
struct BwMapActors { u16 map; const u8 (*ids)[2]; u8 count; };

#include "data/bw_vm_tables.h"
#include "data/bw_param_sizes.h"     // sBwParamSizes[opcode]: parameter sizes (1, 2, 4), 0-terminated

#define BW_ZONE_TEXT 1024            // a message command's "text file" meaning the running script's own
#define BW_PLAYER_ACTOR 255
#define BW_MAX_FRAMES 12
#define BW_STACK_SIZE 16
#define BW_TEMP_VARS 0x100
#define BW_MAX_STEPS 2000            // instructions per frame before yielding (a safety net)

enum BwWait
{
    BW_WAIT_NONE,
    BW_WAIT_FRAMES,
    BW_WAIT_MESSAGE,     // the message is printing
    BW_WAIT_INPUT,       // A or B
    BW_WAIT_MOVEMENT,
    BW_WAIT_YESNO,
    BW_WAIT_FADE,
    BW_WAIT_FANFARE,
    BW_WAIT_CRY,
    BW_WAIT_BATTLE,
    BW_WAIT_FREEZE,
    BW_WAIT_STARTER,     // the starter choice screen
};

struct BwFrame
{
    const u8 *code;
    u32 size;
    u32 pc;
    u16 text;        // the text file its messages come from
    bool8 global;    // a global script (returns with RTEndGlobal)
};

static struct
{
    struct BwFrame frames[BW_MAX_FRAMES];
    u8 depth;
    s32 stack[BW_STACK_SIZE];
    u8 sp;
    u8 cmp;              // last comparison: 0 less, 1 equal, 2 greater
    u8 wait;
    u16 waitFrames;
    u16 yesNoDest;
    u16 battleDest;
    u16 zone;
    u16 talkedActor;
    bool8 messageOpen;
    bool8 locked;
    bool8 immediate;     // a map's on-load script: nothing waits, nothing is shown
    u16 temps[BW_TEMP_VARS];
    u8 movement[64];     // our movement actions for the NPC being moved
    u8 movementActor;    // our local id
} sVm;

// --- variables and flags

static u16 *BwVarPtr(u16 id)
{
    if (id >= 0x4000 && id < 0x4000 + UNOVA_VARS_COUNT)
        return GetVarPointer(VAR_UNOVA_START + (id - 0x4000));
    if (id >= 0x8000 && id < 0x8000 + BW_TEMP_VARS)
        return &sVm.temps[id - 0x8000];
    return NULL;
}

static u16 BwVarGet(u16 id)
{
    u16 *p = BwVarPtr(id);
    return p ? *p : 0;
}

static void BwVarSet(u16 id, u16 value)
{
    u16 *p = BwVarPtr(id);
    if (p)
        *p = value;
}

// A "flex" parameter: a variable's value if it names one, else itself.
static u16 BwFlex(u16 v)
{
    if (v >= 0x4000 && v < 0xC000)
        return BwVarGet(v);
    return v;
}

static u16 BwFlagId(u16 flag)
{
    flag = BwFlex(flag);
    if (flag >= UNOVA_FLAGS_COUNT)
        return 0;
    return FLAG_UNOVA_START + flag;
}

bool32 BwFlagGet(u16 flag)
{
    u16 id = BwFlagId(flag);
    return id ? FlagGet(id) : FALSE;
}

// --- reading the bytecode

static struct BwFrame *Top(void)
{
    return &sVm.frames[sVm.depth - 1];
}

static u8 Read8(void)
{
    struct BwFrame *f = Top();
    return f->pc < f->size ? f->code[f->pc++] : 0;
}

static u16 Read16(void)
{
    u16 lo = Read8();
    return lo | (Read8() << 8);
}

static s32 Read32(void)
{
    u32 lo = Read16();
    return (s32)(lo | ((u32)Read16() << 16));
}

static const u8 *ScriptFileEntry(u16 file, u16 index, u32 *pcOut)
{
    const u8 *d;
    u32 size, n = 0, pos = 0;
    if (file >= ARRAY_COUNT(sBwScriptFiles) || !sBwScriptFiles[file].data)
        return NULL;
    d = sBwScriptFiles[file].data;
    size = sBwScriptFiles[file].size;
    while (pos + 2 <= size && (d[pos] | (d[pos + 1] << 8)) != 0xFD13)
    {
        if (n == index)
        {
            s32 rel = d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16) | ((u32)d[pos + 3] << 24);
            *pcOut = pos + 4 + rel;
            return d;
        }
        n++;
        pos += 4;
    }
    return NULL;
}

// Pushes the frame for script id scrid: the zone's own (1..1999) or a global one.
static bool32 PushScript(u16 scrid, u16 zoneScripts, u16 zoneText, bool8 global)
{
    u32 i, pc;
    u16 file = zoneScripts, text = zoneText, index;
    const u8 *d;
    if (sVm.depth >= BW_MAX_FRAMES || scrid == 0)
        return FALSE;
    index = scrid - 1;
    if (scrid >= 2000)
    {
        for (i = 0; i < ARRAY_COUNT(sBwGlobalRanges); i++)
        {
            if (scrid >= sBwGlobalRanges[i].first && scrid <= sBwGlobalRanges[i].last)
                break;
        }
        if (i == ARRAY_COUNT(sBwGlobalRanges))
            return FALSE;
        file = sBwGlobalRanges[i].file;
        text = sBwGlobalRanges[i].text;
        index = scrid - sBwGlobalRanges[i].first;
        global = TRUE;
    }
    d = ScriptFileEntry(file, index, &pc);
    if (!d)
        return FALSE;
    sVm.frames[sVm.depth].code = d;
    sVm.frames[sVm.depth].size = sBwScriptFiles[file].size;
    sVm.frames[sVm.depth].pc = pc;
    sVm.frames[sVm.depth].text = text;
    sVm.frames[sVm.depth].global = global;
    sVm.depth++;
    return TRUE;
}

// --- text

static const u8 *Message(u16 textFile, u16 msgId)
{
    u16 file = (textFile == BW_ZONE_TEXT) ? Top()->text : textFile;
    msgId = BwFlex(msgId);
    if (file >= ARRAY_COUNT(sBwTextFiles) || !sBwTextFiles[file].strings || msgId >= *sBwTextFiles[file].count)
        return NULL;
    return sBwTextFiles[file].strings[msgId];
}

static void ShowMessage(const u8 *str)
{
    if (!str || sVm.immediate)
        return;
    StringExpandPlaceholders(gStringVar4, str);
    ShowFieldMessage(gStringVar4);
    sVm.messageOpen = TRUE;
    sVm.wait = BW_WAIT_MESSAGE;
}

static u8 *Buffer(u8 index)
{
    static u8 *const sBuffers[] = {gStringVar1, gStringVar2, gStringVar3};
    return sBuffers[index < 3 ? index : 2];
}

// --- actors

static u8 LocalIdOf(u16 actor)
{
    u32 i, j;
    u16 map = ((u8)gSaveBlock1Ptr->location.mapGroup << 8) | (u8)gSaveBlock1Ptr->location.mapNum;
    actor = BwFlex(actor);
    if (actor == BW_PLAYER_ACTOR)
        return LOCALID_PLAYER;
    for (i = 0; i < ARRAY_COUNT(sBwMapActors); i++)
    {
        if (sBwMapActors[i].map != map)
            continue;
        for (j = 0; j < sBwMapActors[i].count; j++)
            if (sBwMapActors[i].ids[j][0] == actor)
                return sBwMapActors[i].ids[j][1];
    }
    return 0;
}

// BW movement codes (up, down, left, right order) -> ours (down, up, left, right).
static u8 MovementAction(u16 code, u8 *count)
{
    static const u8 sDir[4] = {1, 0, 2, 3};
    u8 d = sDir[code & 3];
    *count = 1;
    if (code <= 0x03)
        return MOVEMENT_ACTION_FACE_DOWN + d;
    if (code <= 0x07)
        return MOVEMENT_ACTION_WALK_SLOW_DOWN + d;
    if (code <= 0x0B)
        return MOVEMENT_ACTION_WALK_NORMAL_DOWN + d;
    if (code <= 0x0F)
        return MOVEMENT_ACTION_WALK_FAST_DOWN + d;
    if (code <= 0x13)
        return MOVEMENT_ACTION_WALK_FASTER_DOWN + d;
    if (code <= 0x17)
        return MOVEMENT_ACTION_PLAYER_RUN_DOWN + d;
    if (code <= 0x1B)
        return MOVEMENT_ACTION_WALK_IN_PLACE_SLOW_DOWN + d;
    if (code <= 0x1F)
        return MOVEMENT_ACTION_WALK_IN_PLACE_NORMAL_DOWN + d;
    if (code <= 0x27)
        return MOVEMENT_ACTION_WALK_IN_PLACE_FAST_DOWN + d;
    if (code <= 0x2B)
        return MOVEMENT_ACTION_WALK_IN_PLACE_FASTER_DOWN + d;
    if (code <= 0x33)
        return MOVEMENT_ACTION_JUMP_DOWN + d;
    if (code <= 0x3B)
        return MOVEMENT_ACTION_JUMP_2_DOWN + d;
    if (code == 0x4B)
        return MOVEMENT_ACTION_EMOTE_EXCLAMATION_MARK;
    if (code == 0x45)
        return MOVEMENT_ACTION_SET_INVISIBLE;
    return MOVEMENT_ACTION_DELAY_4;
}

static void StartMovement(u16 actor, const u8 *list, u32 limit)
{
    u32 n = 0, i;
    u8 localId = LocalIdOf(actor);
    while (limit >= 4 && n < sizeof(sVm.movement) - 1)
    {
        u16 code = list[0] | (list[1] << 8);
        u16 count = list[2] | (list[3] << 8);
        u8 per;
        if (code == 0xFE)
            break;
        if (code >= 0x00 && code <= 0x3B)
        {
            u8 a = MovementAction(code, &per);
            for (i = 0; i < (count ? count : 1) && n < sizeof(sVm.movement) - 1; i++)
                sVm.movement[n++] = a;
        }
        else if (code == 0x4B || code == 0x45)
        {
            sVm.movement[n++] = MovementAction(code, &per);
        }
        else
        {
            for (i = 0; i < count && n < sizeof(sVm.movement) - 1; i++)
                sVm.movement[n++] = MOVEMENT_ACTION_DELAY_1;   // waits (counted in frames)
        }
        list += 4;
        limit -= 4;
    }
    sVm.movement[n] = MOVEMENT_ACTION_STEP_END;
    if (localId == 0 || n == 0)
        return;
    sVm.movementActor = localId;
    ScriptMovement_StartObjectMovementScript(localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, sVm.movement);
}

static void SetActorVisible(u16 actor, bool32 visible)
{
    u8 localId = LocalIdOf(actor);
    if (!localId || localId == LOCALID_PLAYER)
        return;
    if (visible)
        TrySpawnObjectEvent(localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup);
    else
        RemoveObjectEventByLocalIdAndMap(localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup);
}

// --- warps: a BW zone and tile -> our map and its local tile

static void WarpTo(u16 zone, u16 x, u16 z)
{
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sBwZoneMaps); i++)
    {
        const struct BwZoneMap *m = &sBwZoneMaps[i];
        if (m->zone == zone && x >= m->x && x < m->x + m->w && z >= m->y && z < m->y + m->h)
        {
            SetWarpDestination(m->map >> 8, m->map & 0xFF, WARP_ID_NONE, x - m->x, z - m->y);
            DoWarp();
            return;
        }
    }
    for (i = 0; i < ARRAY_COUNT(sBwZoneMaps); i++)
    {
        if (sBwZoneMaps[i].zone == zone)
        {
            SetWarpDestination(sBwZoneMaps[i].map >> 8, sBwZoneMaps[i].map & 0xFF, WARP_ID_NONE, x % 32, z % 32);
            DoWarp();
            return;
        }
    }
}

// --- comparisons

static bool32 CondTrue(u8 cond)
{
    switch (cond)
    {
    case 0: return sVm.cmp == 0;
    case 1: return sVm.cmp == 1;
    case 2: return sVm.cmp == 2;
    case 3: return sVm.cmp != 2;
    case 4: return sVm.cmp != 0;
    case 5: return sVm.cmp != 1;
    default: // 0xFF: the stack's result (BW's compiler pushes the negated condition and jumps on it)
        return sVm.sp ? sVm.stack[--sVm.sp] != 0 : FALSE;
    }
}

static void Compare(s32 a, s32 b)
{
    sVm.cmp = (a < b) ? 0 : (a == b) ? 1 : 2;
}

static void Push(s32 v)
{
    if (sVm.sp < BW_STACK_SIZE)
        sVm.stack[sVm.sp++] = v;
}

static s32 Pop(void)
{
    return sVm.sp ? sVm.stack[--sVm.sp] : 0;
}

static s32 StackCompare(u16 op, s32 a, s32 b)
{
    switch (op)
    {
    case 0: return a >= b;
    case 1: return a != b;
    case 2: return a <= b;
    case 3: return a > b;
    case 4: return a < b;
    case 5: return a == b;
    case 6: return a && b;
    case 7: return a || b;
    }
    return 0;
}

static u16 Item(u16 bw)
{
    bw = BwFlex(bw);
    return bw < ARRAY_COUNT(sBwItems) ? sBwItems[bw] : ITEM_NONE;
}

static u16 Species(u16 dex)
{
    dex = BwFlex(dex);
    return dex < ARRAY_COUNT(sBwSpecies) ? sBwSpecies[dex] : SPECIES_NONE;
}

// --- running

static void SkipParams(u16 op)
{
    const u8 *p = op < ARRAY_COUNT(sBwParamSizes) ? sBwParamSizes[op] : NULL;
    while (p && *p)
    {
        u8 n = *p++;
        while (n--)
            Read8();
    }
}

static bool32 PlayerWon(void)
{
    return gBattleOutcome != B_OUTCOME_LOST && gBattleOutcome != B_OUTCOME_DREW && gBattleOutcome != B_OUTCOME_FORFEITED;
}

extern const u8 *const gBwTrainerDefeatTexts[];   // data/unova_trainer_text.s (bw/trainers.py)
extern const u32 gBwTrainerDefeatTextCount;
static const u8 sBwDefeatFallback[] = _("…$");

static u8 *DefeatText(u16 tr)
{
    if (tr < gBwTrainerDefeatTextCount && gBwTrainerDefeatTexts[tr])
        return (u8 *)gBwTrainerDefeatTexts[tr];
    return (u8 *)sBwDefeatFallback;
}

// BW's Poké Mart: the counter's list grows with your badges (BW's "primary" lists), the other
// clerks have fixed lists. sBwShops / sBwShopOffsets come from overlay 21 (bw/vmdata.py).
static u16 sBwShopItems[24];

static bool32 OpenShop(u16 shop)
{
    u32 i, n, start;
    if (shop >= ARRAY_COUNT(sBwShopOffsets) - 1)
    {
        static const u8 sByBadges[] = {0, 16, 16, 20, 20, 23, 23, 24, 25};   // badges -> BW's primary list
        shop = sByBadges[min(PW_CountBadges(REGION_UNOVA), 8)];
    }
    start = sBwShopOffsets[shop];
    n = min(sBwShopOffsets[shop + 1] - start, ARRAY_COUNT(sBwShopItems) - 1);
    for (i = 0; i < n; i++)
        sBwShopItems[i] = sBwShops[start + i];
    sBwShopItems[n] = ITEM_NONE;
    if (n == 0)
        return FALSE;
    if (sVm.messageOpen)
    {
        HideFieldMessageBox();
        sVm.messageOpen = FALSE;
    }
    CreatePokemartMenu(sBwShopItems);
    ScriptContext_Stop();               // the mart re-enables the script when it closes
    return TRUE;
}

#include "data/bw_songs.h"   // sBwSongs: BW sound id -> MUS_BW_* (import_bw.py)

static u16 BwSong(u16 id)
{
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sBwSongs); i++)
    {
        if (sBwSongs[i][0] == id)
            return sBwSongs[i][1];
    }
    return 0;
}

// A battle BW's own script starts (rivals, gym leaders, Team Plasma): BW trainer ids are
// UNOVA_TRAINERS_START + n. Like our trainerbattle with "continue script": the field comes back
// to this script afterwards. canLose: a story battle BW lets you lose (you're healed and it goes on).
static bool32 StartTrainerBattle(u16 tr1, u16 tr2, bool32 canLose)
{
    if (!tr1 || tr1 >= UNOVA_TRAINERS_COUNT)
        return FALSE;
    if (tr2 >= UNOVA_TRAINERS_COUNT)
        tr2 = 0;
    InitTrainerBattleParameter();
    TRAINER_BATTLE_PARAM.opponentA = UNOVA_TRAINERS_START + tr1;
    TRAINER_BATTLE_PARAM.defeatTextA = DefeatText(tr1);
    TRAINER_BATTLE_PARAM.continueScript = TRUE;
    if (tr2)
    {
        TRAINER_BATTLE_PARAM.opponentB = UNOVA_TRAINERS_START + tr2;
        TRAINER_BATTLE_PARAM.defeatTextB = DefeatText(tr2);
        gNoOfApproachingTrainers = 2;
    }
    else
    {
        gNoOfApproachingTrainers = 1;
    }
    if (canLose)
    {
        TRAINER_BATTLE_PARAM.earlyRival = TRUE;
        TRAINER_BATTLE_PARAM.rivalBattleFlags = RIVAL_BATTLE_HEAL_AFTER;
    }
    if (sVm.messageOpen)
    {
        HideFieldMessageBox();
        sVm.messageOpen = FALSE;
    }
    BattleSetup_StartTrainerBattle();   // stops the script context; the battle's end brings it back
    return TRUE;
}

static bool32 Waiting(void)
{
    if (sVm.immediate)
    {
        sVm.wait = BW_WAIT_NONE;
        return FALSE;
    }
    switch (sVm.wait)
    {
    case BW_WAIT_FRAMES:
        if (sVm.waitFrames)
        {
            sVm.waitFrames--;
            return TRUE;
        }
        break;
    case BW_WAIT_MESSAGE:          // like waitmessage: until the text has printed
        if (!IsFieldMessageBoxHidden())
            return TRUE;
        break;
    case BW_WAIT_INPUT:
        if (!JOY_NEW(A_BUTTON | B_BUTTON))
            return TRUE;
        break;
    case BW_WAIT_MOVEMENT:
        if (!ScriptMovement_IsAllObjectMovementFinished())
            return TRUE;
        break;
    case BW_WAIT_YESNO:
        if (ScriptContext_IsEnabled() == FALSE)
            return TRUE;
        BwVarSet(sVm.yesNoDest, gSpecialVar_Result == 1 ? 0 : 1);   // ours: 1 = yes; BW: 0 = yes
        break;
    case BW_WAIT_FADE:
        if (gPaletteFade.active)
            return TRUE;
        break;
    case BW_WAIT_FANFARE:
        if (!IsFanfareTaskInactive())
            return TRUE;
        break;
    case BW_WAIT_CRY:
        if (!IsCryFinished())
            return TRUE;
        break;
    case BW_WAIT_BATTLE:
        if (gMain.callback2 != CB2_Overworld)
            return TRUE;
        BwVarSet(sVm.battleDest, PlayerWon() ? 1 : 0);
        break;
    case BW_WAIT_FREEZE:
        if (!IsFreezePlayerFinished())
            return TRUE;
        break;
    case BW_WAIT_STARTER:
        if (gMain.callback2 != CB2_Overworld)
            return TRUE;
        BwVarSet(sVm.battleDest, gSpecialVar_Result);
        break;
    }
    sVm.wait = BW_WAIT_NONE;
    return FALSE;
}

static void EndScript(void)
{
    if (sVm.messageOpen)
        HideFieldMessageBox();
    if (sVm.locked)
    {
        ScriptMovement_UnfreezeObjectEvents();
        UnfreezeObjectEvents();
    }
    sVm.messageOpen = FALSE;
    sVm.locked = FALSE;
    sVm.depth = 0;
}

// Executes instructions until the script waits or ends. Returns FALSE when it has ended.
static bool32 Run(void)
{
    u32 steps = 0;
    while (sVm.depth)
    {
        struct BwFrame *f;
        u16 op;
        if (sVm.wait != BW_WAIT_NONE && Waiting())
            return TRUE;
        if (++steps > BW_MAX_STEPS)
            return TRUE;
        f = Top();
        if (f->pc + 2 > f->size)
        {
            sVm.depth--;
            continue;
        }
        op = Read16();
        switch (op)
        {
        case 0x02: // VMHalt
            sVm.depth = 0;
            break;
        case 0x03: // VMSleep
            sVm.waitFrames = Read16() * 2;   // BW's field runs at 30 fps
            sVm.wait = BW_WAIT_FRAMES;
            break;
        case 0x04: // VMCall
        {
            s32 rel = Read32();
            if (sVm.depth < BW_MAX_FRAMES)
            {
                sVm.frames[sVm.depth] = *f;
                sVm.frames[sVm.depth].global = FALSE;
                sVm.frames[sVm.depth].pc = f->pc + rel;
                sVm.depth++;
            }
            break;
        }
        case 0x05: // VMReturn
            sVm.depth--;
            break;
        case 0x08: Push(Read16()); break;                       // StackPushConst
        case 0x09: Push(BwFlex(Read16())); break;                // StackPush
        case 0x0A: BwVarSet(Read16(), Pop()); break;             // StackPop
        case 0x0B: Pop(); break;                                 // StackDiscard
        case 0x0C: { s32 b = Pop(), a = Pop(); Push(a + b); break; }
        case 0x0D: { s32 b = Pop(), a = Pop(); Push(a - b); break; }
        case 0x0E: { s32 b = Pop(), a = Pop(); Push(a * b); break; }
        case 0x0F: { s32 b = Pop(), a = Pop(); Push(b ? a / b : 0); break; }
        case 0x10: Push(BwFlagGet(Read16())); break;             // StackPushFlag
        case 0x11: { u16 c = Read16(); s32 b = Pop(), a = Pop(); Push(StackCompare(c, a, b)); break; }
        case 0x12: { u16 v = Read16(); BwVarSet(v, BwVarGet(v) & BwFlex(Read16())); break; }
        case 0x13: { u16 v = Read16(); BwVarSet(v, BwVarGet(v) | BwFlex(Read16())); break; }
        case 0x19: { u16 v = Read16(); Compare(BwVarGet(v), Read16()); break; }      // WorkCmpConst
        case 0x1A: { u16 a = Read16(); Compare(BwVarGet(a), BwVarGet(Read16())); break; }
        case 0x1B: // RTCallGlobalAsync
        case 0x1C: // RTCallGlobal
            PushScript(Read16(), sBwZones[sVm.zone].scripts, sBwZones[sVm.zone].text, TRUE);
            break;
        case 0x1D: // RTEndGlobal
            while (sVm.depth && !Top()->global)
                sVm.depth--;
            if (sVm.depth)
                sVm.depth--;
            break;
        case 0x1E: // VMJump
        {
            s32 rel = Read32();
            f->pc += rel;
            break;
        }
        case 0x1F: // VMJumpIf
        {
            u8 cond = Read8();
            s32 rel = Read32();
            if (CondTrue(cond))
                f->pc += rel;
            break;
        }
        case 0x20: // VMCallIf
        {
            u8 cond = Read8();
            s32 rel = Read32();
            if (CondTrue(cond) && sVm.depth < BW_MAX_FRAMES)
            {
                sVm.frames[sVm.depth] = *f;
                sVm.frames[sVm.depth].global = FALSE;
                sVm.frames[sVm.depth].pc = f->pc + rel;
                sVm.depth++;
            }
            break;
        }
        case 0x23: { u16 id = BwFlagId(Read16()); if (id) FlagSet(id); break; }
        case 0x24: { u16 id = BwFlagId(Read16()); if (id) FlagClear(id); break; }
        case 0x25: { u16 fl = Read16(); BwVarSet(Read16(), BwFlagGet(fl)); break; }
        case 0x26: { u16 v = Read16(); BwVarSet(v, BwVarGet(v) + BwFlex(Read16())); break; }
        case 0x27: { u16 v = Read16(); BwVarSet(v, BwVarGet(v) - BwFlex(Read16())); break; }
        case 0x28: { u16 v = Read16(); BwVarSet(v, Read16()); break; }               // WorkSetConst
        case 0x29: { u16 v = Read16(); BwVarSet(v, BwVarGet(Read16())); break; }     // WorkGet
        case 0x2A: { u16 v = Read16(); BwVarSet(v, BwFlex(Read16())); break; }       // WorkSet
        case 0x2B: { u16 v = Read16(); BwVarSet(v, BwVarGet(v) * BwFlex(Read16())); break; }
        case 0x2C: { u16 v = Read16(), d = BwFlex(Read16()); BwVarSet(v, d ? BwVarGet(v) / d : 0); break; }
        case 0x2D: { u16 v = Read16(), d = BwFlex(Read16()); BwVarSet(v, d ? BwVarGet(v) % d : 0); break; }
        case 0x2E: // ActorPauseAll
            if (!sVm.locked)
            {
                FreezeObjects_WaitForPlayer();
                sVm.locked = TRUE;
                sVm.wait = BW_WAIT_FREEZE;
            }
            break;
        case 0x2F: // ActorUnlockAll
            ScriptMovement_UnfreezeObjectEvents();
            UnfreezeObjectEvents();
            sVm.locked = FALSE;
            break;
        case 0x31: // InputWaitAB
        case 0x32: // InputWaitLast
            sVm.wait = BW_WAIT_INPUT;
            break;
        case 0x34: case 0x35: // MsgSystem(Async)
        {
            u16 msg = Read16();
            Read16();
            ShowMessage(Message(BW_ZONE_TEXT, msg));
            break;
        }
        case 0x38: { u16 msg = Read16(); Read8(); ShowMessage(Message(BW_ZONE_TEXT, msg)); break; }   // MsgInfo
        case 0x3C: // MsgActorEx
        {
            u16 file = Read16(), msg = Read16();
            Read16(); Read16(); Read16();
            ShowMessage(Message(file, msg));
            break;
        }
        case 0x3D: // MsgActor
        {
            u16 file = Read16(), msg = Read16();
            Read16(); Read16();
            ShowMessage(Message(file, msg));
            break;
        }
        case 0x43: { u16 msg = Read16(); Read16(); ShowMessage(Message(BW_ZONE_TEXT, msg)); break; }  // MsgPlaceSign
        case 0x48: // MsgActorGendered
        case 0x49: // MsgActorVersioned (Black: the second)
        {
            u16 file = Read16(), a = Read16(), b = Read16();
            Read16(); Read16(); Read16();
            ShowMessage(Message(file, op == 0x49 ? b : (gSaveBlock2Ptr->playerGender == MALE ? a : b)));
            break;
        }
        case 0x4A: { u16 msg = Read16(); Read8(); ShowMessage(Message(BW_ZONE_TEXT, msg)); break; }   // MsgScream
        case 0x36: case 0x39: case 0x3E: case 0x3F: case 0x44: case 0x4B: // the message closes
            HideFieldMessageBox();
            sVm.messageOpen = FALSE;
            break;
        case 0x47: // YesNoWin
            sVm.yesNoDest = Read16();
            if (!sVm.immediate && ScriptMenu_YesNo(20, 8))
            {
                ScriptContext_Stop();
                sVm.wait = BW_WAIT_YESNO;
            }
            break;
        case 0x4C: StringCopy(Buffer(Read8()), gSaveBlock2Ptr->playerName); break;     // WordSetPlayerName
        case 0x4D: case 0x4F: case 0x50: case 0x52: // item names
        {
            u8 b = Read8();
            CopyItemName(Item(Read16()), Buffer(b));
            break;
        }
        case 0x4E: { u8 b = Read8(); CopyItemName(Item(Read16()), Buffer(b)); Read16(); Read8(); break; }
        case 0x57: case 0x58: // WordSetPokeSpecies(WithArticle)
        {
            u8 b = Read8();
            StringCopy(Buffer(b), GetSpeciesName(Species(Read16())));
            break;
        }
        case 0x5C: // WordSetNumber
        {
            u8 b = Read8();
            u16 v = BwFlex(Read16());
            u16 digits = BwFlex(Read16());
            ConvertIntToDecimalStringN(Buffer(b), v, STR_CONV_MODE_LEFT_ALIGN, digits ? digits : 5);
            break;
        }
        case 0x64: // ActorCmdExec
        {
            u16 actor = Read16();
            s32 rel = Read32();
            u32 at = f->pc + rel;
            if (at < f->size)
                StartMovement(actor, f->code + at, f->size - at);
            break;
        }
        case 0x65: // ActorCmdWait
            sVm.wait = BW_WAIT_MOVEMENT;
            break;
        case 0x68: // PlayerGetGPos: in BW matrix tiles
        {
            u16 vx = Read16(), vz = Read16();
            s16 x = gSaveBlock1Ptr->pos.x, y = gSaveBlock1Ptr->pos.y;
            u32 i;
            u16 map = ((u8)gSaveBlock1Ptr->location.mapGroup << 8) | (u8)gSaveBlock1Ptr->location.mapNum;
            for (i = 0; i < ARRAY_COUNT(sBwZoneMaps); i++)
                if (sBwZoneMaps[i].map == map)
                {
                    x += sBwZoneMaps[i].x;
                    y += sBwZoneMaps[i].y;
                    break;
                }
            BwVarSet(vx, x);
            BwVarSet(vz, y);
            break;
        }
        case 0x6B: SetActorVisible(Read16(), TRUE); break;      // ActorAdd
        case 0x6C: SetActorVisible(Read16(), FALSE); break;     // ActorDelete
        case 0x6E: // PlayerGetDir: BW 0 up, 1 down, 2 left, 3 right
        {
            static const u8 sToBw[] = {[DIR_NORTH] = 0, [DIR_SOUTH] = 1, [DIR_WEST] = 2, [DIR_EAST] = 3};
            u8 d = GetPlayerFacingDirection();
            BwVarSet(Read16(), d < ARRAY_COUNT(sToBw) ? sToBw[d] : 1);
            break;
        }
        case 0x74: // ActorSetEyeToEye
            if (gObjectEvents[gSelectedObjectEvent].active)
                ObjectEventFaceOppositeDirection(&gObjectEvents[gSelectedObjectEvent], GetPlayerFacingDirection());
            break;
        case 0x85: // CallTrainerBattle: trainer 1, trainer 2 (0: none), flags (1: losing is allowed)
        case 0x86: // CallTrainerMultiBattle: ally, trainer 1, trainer 2, flags (the ally isn't ours: as 0x85)
        {
            u16 tr1, tr2, flags;
            if (op == 0x86)
                Read16();
            tr1 = BwFlex(Read16());
            tr2 = BwFlex(Read16());
            flags = BwFlex(Read16());
            if (StartTrainerBattle(tr1, tr2, flags & 1))
            {
                sVm.battleDest = 0;
                sVm.wait = BW_WAIT_BATTLE;
                return TRUE;
            }
            break;
        }
        case 0x8C: // CallTrainerLose: our battle end already whited out when that was due
            break;
        case 0x8D: { u16 dest = Read16(); BwVarSet(dest, PlayerWon()); break; }   // TrainerBattleIsVictory
        case 0x95: { u16 t = BwFlex(Read16()); if (t && t < UNOVA_TRAINERS_COUNT) FlagSet(GetTrainerFlagId(UNOVA_TRAINERS_START + t)); break; }
        case 0x96: { u16 t = BwFlex(Read16()); if (t && t < UNOVA_TRAINERS_COUNT) FlagClear(GetTrainerFlagId(UNOVA_TRAINERS_START + t)); break; }
        case 0x97: // TrainerFlagGet
        {
            u16 t = BwFlex(Read16()), dest = Read16();
            BwVarSet(dest, t && t < UNOVA_TRAINERS_COUNT && FlagGet(GetTrainerFlagId(UNOVA_TRAINERS_START + t)));
            break;
        }
        case 0xA9: // MEPlay
            Read16();
            PlayFanfare(MUS_OBTAIN_ITEM);
            break;
        case 0xAA: sVm.wait = BW_WAIT_FANFARE; break;          // MEWait
        case 0xAB: { u16 sp = Species(Read16()); Read16(); if (sp) PlayCry_Normal(sp, 0); break; }
        case 0xAC: sVm.wait = BW_WAIT_CRY; break;              // PVWait
        case 0xB3: // FadeEx: mode (1/2 black, 4/8 white), start, end, slowness
        {
            u16 mode = Read16(), from = Read16(), to = Read16();
            Read16();
            BeginNormalPaletteFade(PALETTES_ALL, 0, from ? 16 : 0, to ? 16 : 0, (mode & 0xC) ? RGB_WHITE : RGB_BLACK);
            break;
        }
        case 0xB4: sVm.wait = BW_WAIT_FADE; break;             // FadeExWait
        case 0xB5: // ItemAdd
        {
            u16 it = Item(Read16()), n = BwFlex(Read16()), dest = Read16();
            BwVarSet(dest, it && AddBagItem(it, n));
            break;
        }
        case 0xB6: { u16 it = Item(Read16()), n = BwFlex(Read16()), dest = Read16(); BwVarSet(dest, it && RemoveBagItem(it, n)); break; }
        case 0xB7: { u16 it = Item(Read16()), n = BwFlex(Read16()), dest = Read16(); BwVarSet(dest, it && CheckBagHasSpace(it, n)); break; }
        case 0xB8: { u16 it = Item(Read16()), n = BwFlex(Read16()), dest = Read16(); BwVarSet(dest, it && CheckBagHasItem(it, n)); break; }
        case 0xB9: { u16 it = Item(Read16()), dest = Read16(); BwVarSet(dest, it ? CountTotalItemQuantityInBag(it) : 0); break; }
        case 0xC2: // MapChangeWarp
        {
            u16 zone = Read16(), x = Read16(), z = Read16();
            Read16();
            EndScript();
            // like warp + waitstate: the new map starts a fresh script context
            WarpTo(zone, x, z);
            ResetInitialPlayerAvatarState();
            ScriptContext_Stop();
            return TRUE;
        }
        case 0xCB: { u16 v = Read16(); u16 b = BwFlex(Read16()); BwVarSet(v, b ? Random() % b : 0); break; }
        case 0xD5: { u16 dest = Read16(), b = BwFlex(Read16()); BwVarSet(dest, b < 8 && PW_HasBadge(REGION_UNOVA, b)); break; } // TrainerCardHasBadge
        case 0xD6: { u16 b = BwFlex(Read16()); if (b < 8) FlagSet(FLAG_UNOVA_BADGE01_GET + b); break; }     // TrainerCardAddBadge
        case 0xD7: BwVarSet(Read16(), PW_CountBadges(REGION_UNOVA)); break;                                 // TrainerCardGetBadgeCount
        case 0x153: // CallPoke3Select: the starters' gift box -> 0, 1, 2 (Snivy, Tepig, Oshawott)
            sVm.battleDest = Read16();
            if (sVm.messageOpen)
            {
                HideFieldMessageBox();
                sVm.messageOpen = FALSE;
            }
            gMain.savedCallback = CB2_ReturnToFieldContinueScript;
            SetMainCallback2(CB2_ChooseStarter);
            ScriptContext_Stop();
            sVm.wait = BW_WAIT_STARTER;
            return TRUE;
        case 0x98: case 0x9F: // BGMPlay, BGMPlayPush: BW song id -> ours, if it was converted
        {
            u16 song = BwSong(Read16());
            if (song)
                PlayNewMapMusic(song);
            break;
        }
        case 0x9E: // BGMChangeMap: back to the map's music
            PlayNewMapMusic(GetCurrLocationDefaultMusic());
            break;
        case 0x149: // CallFriendlyShopBuy: BW's shop list n (bw/vmdata.py), or the Poké Mart's for your badges
        {
            u16 shop = BwFlex(Read16());
            Read16();
            if (OpenShop(shop))
                return TRUE;
            break;
        }
        case 0xE0: BwVarSet(Read16(), 1); break;               // GameGetVersion: Black
        case 0xE1: BwVarSet(Read16(), gSaveBlock2Ptr->playerGender); break;
        case 0x104: HealPlayerParty(); break;
        case 0x10C: // PokePartyAdd
        {
            u16 dest = Read16(), sp = Species(Read16());
            u16 form, level;
            form = Read16();
            level = BwFlex(Read16());
            (void)form;
            BwVarSet(dest, sp && ScriptGiveMon(sp, level, ITEM_NONE) != MON_CANT_GIVE);
            break;
        }
        case 0x10E: // PokePartyAddEx
        {
            u16 dest = Read16(), sp = Species(Read16()), level;
            Read16();
            level = BwFlex(Read16());
            Read16(); Read16(); Read16();
            {
                u16 item = Item(Read16());
                Read16();
                BwVarSet(dest, sp && ScriptGiveMon(sp, level, item) != MON_CANT_GIVE);
            }
            break;
        }
        case 0x178: // CallWildBattle
        {
            u16 sp = Species(Read16()), level = BwFlex(Read16());
            Read16();
            if (sp)
            {
                // like dowildbattle: the field goes away; the script carries on when it's back
                CreateScriptedWildMon(sp, level, ITEM_NONE);
                BattleSetup_StartScriptedWildBattle();
                sVm.battleDest = 0;
                sVm.wait = BW_WAIT_BATTLE;
                ScriptContext_Stop();
                return TRUE;
            }
            break;
        }
        case 0x17B: { u16 dest = Read16(); BwVarSet(dest, PlayerWon()); break; }
        default:
            SkipParams(op);
            break;
        }
    }
    EndScript();
    return FALSE;
}

static bool8 BwScript_Native(void)
{
    return Run() == FALSE;
}

static bool32 Start(struct ScriptContext *ctx, bool32 immediate)
{
    u16 zone = ScriptReadHalfword(ctx);
    u16 scrid = ScriptReadHalfword(ctx);
    memset(&sVm, 0, offsetof(typeof(sVm), temps));
    if (zone >= ARRAY_COUNT(sBwZones))
        return FALSE;
    sVm.zone = zone;
    sVm.immediate = immediate;
    return PushScript(scrid, sBwZones[zone].scripts, sBwZones[zone].text, FALSE);
}

// bwscript <zone>, <script id>: runs BW script `id` of BW zone `zone` (asm/macros/event.inc).
void Script_BwScript(struct ScriptContext *ctx)
{
    if (!Start(ctx, FALSE))
        return;
    SetupNativeScript(ctx, BwScript_Native);
    ctx->waitAfterCallNative = TRUE;
}

// bwscript_init: the same, run to its end right away (a map's on-load scripts).
void Script_BwScriptInit(struct ScriptContext *ctx)
{
    if (!Start(ctx, TRUE))
        return;
    while (Run())
        ;
}

#include "data/bw_start_flags.h"
#include "data/bw_season_music.h"   // sBwSeasonMusic: spring, summer, autumn, winter (bw/music.py)

// A Unova song with seasonal versions -> this month's (BW's seasons: Jan/May/Sep spring, and so on).
u16 BwSeasonMusic(u16 song)
{
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sBwSeasonMusic); i++)
    {
        if (sBwSeasonMusic[i][0] == song)
            return sBwSeasonMusic[i][(GetMonth() - MONTH_JAN) % 4];
    }
    return song;
}

// The first time the player is in Unova: hide the characters who only appear later in BW's story.
void BwScript_OnEnterMap(void)
{
    u32 i;
    if (GetRegionForSectionId(gMapHeader.regionMapSectionId) != REGION_UNOVA || FlagGet(FLAG_UNOVA_INITIALIZED))
        return;
    for (i = 0; i < ARRAY_COUNT(sBwStartFlags); i++)
        FlagSet(FLAG_UNOVA_START + sBwStartFlags[i]);
    FlagSet(FLAG_UNOVA_INITIALIZED);
}
