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
#include "m4a.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "constants/party_menu.h"
#include "constants/script_menu.h"
#include "constants/rtc.h"
#include "strings.h"
#include "platform/web3d.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "pokerus.h"
#include "pokedex.h"
#include "map_name_popup.h"
#include "list_menu.h"
#include "party_menu.h"
#include "malloc.h"
#include "fieldmap.h"
#include "money.h"
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
#include "line_break.h"
#include "text.h"
#include "trainer_see.h"
#include "tv.h"
void ChangePokemonNickname(void);   // src/tv.c (a special, not in tv.h)
#include "constants/battle.h"
#include "constants/event_objects.h"
#include "constants/opponents.h"
#include "constants/flags.h"
#include "constants/rgb.h"
#include "constants/items.h"
#include "constants/map_groups.h"
#include "constants/songs.h"
#include "constants/vars.h"

#ifndef BW_TRACE_ON
#define BW_TRACE_ON 0   // 1: log BW scripts to the browser console
#endif
// BW_TRACE: script starts/ends and skipped commands to the browser console (debugging cutscenes)
#if defined(PLATFORM_WEB) && BW_TRACE_ON
extern void emscripten_log(int flags, const char *format, ...);   // <emscripten.h> clashes with our headers
#define BW_TRACE(...) emscripten_log(1, __VA_ARGS__)
#else
#define BW_TRACE(...)
#endif

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
    BW_WAIT_MENU,        // a list menu (ListMenu*)
    BW_WAIT_PARTY,       // choosing a party Pokémon (CallPokeSelect)
    BW_WAIT_SE,          // a sound effect (SEWait)
    BW_WAIT_DEMO,        // a BW 3D cutscene playing (Call3DDemo)
    BW_WAIT_PROP,        // a building's animation (StaticProp.HndWaitAnm)
    BW_WAIT_CALL,        // an Xtransceiver call, line by line (CallXTransceiver)
    BW_WAIT_FIELD,       // an app (the naming screen...) until we're back on the map
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
    u8 movementActor;    // our local id (the last one moved)
    u16 reserved;        // RTReserveScript: run after this one ends
    u16 cameraFrames;    // frames left of the event camera's move (EVCameraWait)
    u16 menuDest, menuDefault;
    u8 menuX, menuY, menuCount;
    bool8 menuCancel;
    u16 partyDest;
    u16 demoFrames;
    u8 callId, callLine;
    u32 demoWaited;
    struct ListMenuItem *menuItems;
} sVm;

// Our movement actions, one list per moving actor: BW often starts several actors' movements
// before waiting, and a movement script reads its list while it runs.
#define BW_MOVE_SLOTS 16
static u8 sMoveLists[BW_MOVE_SLOTS][64];
static u16 sMoveOwner[BW_MOVE_SLOTS];  // local id + 1 (the player is 0xFF), 0 = free
static u8 sMoveNext;

// Each actor keeps its own list: the engine's movement script for an actor keeps pointing at it
// even after it's done, so another actor's list must never be written there.
static u8 *MoveListFor(u8 localId)
{
    u32 i;
    for (i = 0; i < BW_MOVE_SLOTS; i++)
        if (sMoveOwner[i] == localId + 1)
            return sMoveLists[i];
    for (i = 0; i < BW_MOVE_SLOTS; i++)
        if (sMoveOwner[i] == 0)
            break;
    if (i == BW_MOVE_SLOTS)
    {
        i = sMoveNext;
        sMoveNext = (sMoveNext + 1) % BW_MOVE_SLOTS;
    }
    sMoveOwner[i] = localId + 1;
    return sMoveLists[i];
}

// Actors BW's scripts add with ActorNew: BW actor id -> our local id, for this map.
#define BW_DYN_ACTORS 8
#define BW_DYN_LOCALID_FIRST 0xE0
static u16 sDynActors[BW_DYN_ACTORS];   // BW actor id + 1 (0 = free)

#include "data/bw_obj_gfx.h"   // sBwObjGfx: BW objCode -> our graphics id (bw/sprites.py)

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

// BW's lines are laid out for the DS's wider text box: a page with a line too wide for ours is
// wrapped again to fit (two lines, scrolling like ours). Pages with control codes (colours...)
// keep BW's breaks, since the line breaker can't step over them.
#define BW_TEXT_WIDTH 208   // our field message box (27 tiles), less room for the "more" arrow

static bool32 PageTooWide(u8 *page)
{
    u8 *p = page, *line = page;
    for (;; p++)
    {
        u8 c = *p;
        if (c == EXT_CTRL_CODE_BEGIN || c == PLACEHOLDER_BEGIN)
            return FALSE;
        if (c == EOS || c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL)
        {
            s32 w;
            *p = EOS;
            w = GetStringWidth(FONT_NORMAL, line, 0);
            *p = c;
            if (w > BW_TEXT_WIDTH)
                return TRUE;
            if (c == EOS)
                return FALSE;
            line = p + 1;
        }
    }
}

static void FitPage(u8 *page)
{
    u8 *r, *w;
    bool32 space = TRUE;
    if (!PageTooWide(page))
        return;
    StripLineBreaks(page);
    // one space between words, none at the ends (the line breaker counts one per gap)
    for (r = w = page; *r != EOS; r++)
    {
        if (*r == CHAR_SPACE)
        {
            if (space)
                continue;
            space = TRUE;
        }
        else
        {
            space = FALSE;
        }
        *w++ = *r;
    }
    if (w > page && w[-1] == CHAR_SPACE)
        w--;
    *w = EOS;
    BreakSubStringAutomatic(page, BW_TEXT_WIDTH, 2, FONT_NORMAL, SHOW_SCROLL_PROMPT);
}

static EWRAM_DATA u8 sBwExpanded[1000] = {0};

static void FitMessageToBox(u8 *dst, const u8 *src)
{
    for (;;)
    {
        u8 *page = dst;
        while (*src != EOS && *src != CHAR_PROMPT_CLEAR)
            *dst++ = *src++;
        *dst = EOS;
        FitPage(page);
        dst = page + StringLength(page);
        if (*src == EOS)
            break;
        *dst++ = *src++;   // the page break
    }
    *dst = EOS;
}

static void ShowMessage(const u8 *str)
{
    if (!str || sVm.immediate)
        return;
    StringExpandPlaceholders(sBwExpanded, str);
    FitMessageToBox(gStringVar4, sBwExpanded);
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
    for (i = 0; i < BW_DYN_ACTORS; i++)
        if (sDynActors[i] == actor + 1)
            return BW_DYN_LOCALID_FIRST + i;
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

#include "data/bw_movement.h"   // sBwMoveCodes: BW movement code -> ours (bw/movement.py)

// appends a wait of the given number of frames to a movement list
static u32 AddDelay(u8 *moves, u32 n, u32 frames)
{
    static const u8 sSteps[] = {16, 8, 4, 2, 1};
    static const u8 sActs[] = {MOVEMENT_ACTION_DELAY_16, MOVEMENT_ACTION_DELAY_8, MOVEMENT_ACTION_DELAY_4,
                               MOVEMENT_ACTION_DELAY_2, MOVEMENT_ACTION_DELAY_1};
    u32 k;
    for (k = 0; k < ARRAY_COUNT(sSteps) && n < 63; k++)
        while (frames >= sSteps[k] && n < 63)
        {
            moves[n++] = sActs[k];
            frames -= sSteps[k];
        }
    return n;
}

static void StartMovement(u16 actor, const u8 *list, u32 limit)
{
    u32 n = 0, i;
    u8 localId = LocalIdOf(actor);
    u8 *moves = localId ? MoveListFor(localId) : sMoveLists[BW_MOVE_SLOTS - 1];
    while (limit >= 4 && n < 63)
    {
        u16 code = list[0] | (list[1] << 8);
        u16 count = list[2] | (list[3] << 8);
        const struct BwMoveCode *c;
        if (code == 0xFE)
            break;
        list += 4;
        limit -= 4;
        if (code > 0xFF)
            continue;
        c = &sBwMoveCodes[code];
        switch (c->kind)
        {
        case 1: // a step, turn or jump: count times
            for (i = 0; i < (count ? count : 1) && n < 63; i++)
                moves[n++] = c->action;
            break;
        case 2:
            moves[n++] = c->action;
            break;
        case 3: // wait: frames x count
            n = AddDelay(moves, n, c->frames * (count ? count : 1));
            break;
        }
    }
    moves[n] = MOVEMENT_ACTION_STEP_END;
    if (localId == 0 || n == 0)
        return;
    sVm.movementActor = localId;
    BW_TRACE("[bw] move %d: %d actions: %d %d %d %d %d %d", localId, n, moves[0], moves[1], moves[2], moves[3], moves[4], moves[5]);
#if BW_TRACE_ON
    {
        u32 k;
        for (k = 0; k < OBJECT_EVENTS_COUNT; k++)
            if (gObjectEvents[k].active && gObjectEvents[k].localId < 3)
                BW_TRACE("[bw]   obj %d at %d,%d", gObjectEvents[k].localId, gObjectEvents[k].currentCoords.x - MAP_OFFSET, gObjectEvents[k].currentCoords.y - MAP_OFFSET);
    }
#endif
    ScriptMovement_StartObjectMovementScript(localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, moves);
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

// --- positions: BW matrix tiles <-> this map's tiles

static const struct BwZoneMap *CurrentZoneMap(void)
{
    u32 i;
    u16 map = ((u8)gSaveBlock1Ptr->location.mapGroup << 8) | (u8)gSaveBlock1Ptr->location.mapNum;
    for (i = 0; i < ARRAY_COUNT(sBwZoneMaps); i++)
        if (sBwZoneMaps[i].map == map)
            return &sBwZoneMaps[i];
    return NULL;
}

static void ToLocal(u16 x, u16 z, s16 *lx, s16 *ly)
{
    const struct BwZoneMap *m = CurrentZoneMap();
    *lx = (s16)x - (m ? m->x : 0);
    *ly = (s16)z - (m ? m->y : 0);
}

static u8 OurDir(u16 bwDir)
{
    static const u8 sDirs[4] = {DIR_NORTH, DIR_SOUTH, DIR_WEST, DIR_EAST};
    return sDirs[bwDir & 3];
}

static struct ObjectEvent *ActorObject(u16 actor)
{
    u8 id, localId = LocalIdOf(actor);
    if (!localId)
        return NULL;
    if (localId == LOCALID_PLAYER)
        return &gObjectEvents[gPlayerAvatar.objectEventId];
    if (TryGetObjectEventIdByLocalIdAndMap(localId, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, &id))
        return NULL;
    return &gObjectEvents[id];
}

// Companions: BW's command 0x241 makes Cheren (actor 250) and Bianca (actor 240) walk with you
// out of Nuvema Town and into Route 1's first scene, across the map change; ActorDelete ends it.
#define BW_COMPANION_CHEREN 250
#define BW_COMPANION_BIANCA 240
static const u16 sCompanionObj[][2] = {{BW_COMPANION_CHEREN, 7}, {BW_COMPANION_BIANCA, 134}};
static EWRAM_DATA u16 sCompanions[2] = {0};   // actor id + 1 of each one walking with you
static EWRAM_DATA bool8 sCompanionsSpawned = FALSE;

static void NewActor(u16 x, u16 z, u16 dir, u16 actor, u16 objCode);

// where a companion stands next to the player: Cheren a step ahead on the left, Bianca on the
// right (from the Nuvema scene's choreography: they face each other across the player, and
// Bianca steps up beside the player while Cheren is already a row ahead)
static void SpawnCompanion(u32 k)
{
    const struct BwZoneMap *m = CurrentZoneMap();
    s16 px = gSaveBlock1Ptr->pos.x + (m ? m->x : 0), py = gSaveBlock1Ptr->pos.y + (m ? m->y : 0);
    u16 actor = sCompanionObj[k][0];
    if (sVm.zone == 389)      // Nuvema's exit scene first walks the player to (788, 737), x = 786 + 2
    {
        px = 788;
        py = 737;
    }
    if (actor == BW_COMPANION_CHEREN)
        NewActor(px - 1, py - 1, 1, actor, sCompanionObj[k][1]);
    else
        NewActor(px + 1, py, 1, actor, sCompanionObj[k][1]);
}

static void AddCompanion(u16 actor)
{
    u32 k;
    for (k = 0; k < ARRAY_COUNT(sCompanionObj); k++)
        if (sCompanionObj[k][0] == actor)
        {
            sCompanions[k] = actor + 1;
            SpawnCompanion(k);
        }
}

// a script starting on a new map: bring the companions along
static void SpawnCompanionsHere(void)
{
    u32 k;
    if (sCompanionsSpawned)
        return;
    sCompanionsSpawned = TRUE;
    for (k = 0; k < ARRAY_COUNT(sCompanionObj); k++)
        if (sCompanions[k])
            SpawnCompanion(k);
}

static void DropCompanion(u16 actor)
{
    u32 k;
    for (k = 0; k < ARRAY_COUNT(sCompanionObj); k++)
        if (sCompanions[k] == actor + 1)
            sCompanions[k] = 0;
}

// ActorNew: a character a cutscene brings in
static void NewActor(u16 x, u16 z, u16 dir, u16 actor, u16 objCode)
{
    u32 i, gfx = OBJ_EVENT_GFX_BOY_1;
    s16 lx, ly;
    for (i = 0; i < ARRAY_COUNT(sBwObjGfx); i++)
        if (sBwObjGfx[i][0] == objCode)
            gfx = sBwObjGfx[i][1];
    for (i = 0; i < BW_DYN_ACTORS; i++)
        if (sDynActors[i] == 0 || sDynActors[i] == actor + 1)
            break;
    if (i == BW_DYN_ACTORS)
        return;
    if (sDynActors[i])
        RemoveObjectEventByLocalIdAndMap(BW_DYN_LOCALID_FIRST + i, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup);
    sDynActors[i] = actor + 1;
    ToLocal(x, z, &lx, &ly);
    SpawnSpecialObjectEventParameterized(gfx, MOVEMENT_TYPE_FACE_DOWN, BW_DYN_LOCALID_FIRST + i,
                                         lx + MAP_OFFSET, ly + MAP_OFFSET, 3);
    {
        struct ObjectEvent *o = ActorObject(actor);
        if (o)
            ObjectEventTurn(o, OurDir(dir));
    }
}

static void SetActorPos(u16 actor, u16 x, u16 z, u16 dir, bool32 turn)
{
    struct ObjectEvent *o = ActorObject(actor);
    s16 lx, ly;
    if (!o || LocalIdOf(actor) == LOCALID_PLAYER)
        return;            // the player stays where they are (moving them would leave the camera behind)
    ToLocal(x, z, &lx, &ly);
    MoveObjectEventToMapCoords(o, lx + MAP_OFFSET, ly + MAP_OFFSET);
    if (turn)
        ObjectEventTurn(o, OurDir(dir));
}

// a list menu option: BW's text, in our box
static void MenuAdd(u16 msg, u16 uid)
{
    const u8 *str = Message(BW_ZONE_TEXT, msg);
    u8 *name;
    if (!sVm.menuItems)
        sVm.menuItems = AllocZeroed(sizeof(struct ListMenuItem) * 16);
    if (!sVm.menuItems || sVm.menuCount >= 16)
        return;
    if (!str)
        return;
    name = Alloc(100);
    StringExpandPlaceholders(name, str);
    sVm.menuItems[sVm.menuCount].name = name;
    sVm.menuItems[sVm.menuCount].id = uid;
    sVm.menuCount++;
}

static bool32 MenuShow(void)
{
    u32 i, row = 0;
    if (!sVm.menuCount || !sVm.menuItems)
        return FALSE;
    for (i = 0; i < sVm.menuCount; i++)
        if (sVm.menuItems[i].id == sVm.menuDefault)
            row = i;
    if (!ScriptMenu_MultichoiceDynamic(sVm.menuX, sVm.menuY, sVm.menuCount, sVm.menuItems, !sVm.menuCancel, 6, row, 0))
        return FALSE;
    sVm.menuItems = NULL;      // the menu frees them
    sVm.menuCount = 0;
    ScriptContext_Stop();
    sVm.wait = BW_WAIT_MENU;
    return TRUE;
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


// --- support for the commands checked against Black's handlers (below)

// MapReplace: BW swaps parts of a map for other versions (the player's room after the first
// battle...). Its on/off states are kept in BW flags past BW's own (2765 on are unused).
#define BW_MAP_REPLACE_FLAG  2780
#define BW_START_FLAGS_FIXED_FLAG 2779   // set once the start flags are Black's own (BwScript_OnEnterMap)
#define BW_MAP_REPLACE_COUNT 32

static EWRAM_DATA u16 sBwPushedSong = 0;
static EWRAM_DATA struct { bool8 used; u16 type, x, z; } sBwProps[8] = {0};   // StaticProp handles

u32 BwMapReplaceBits(void)
{
    u32 i, bits = 0;
    for (i = 0; i < BW_MAP_REPLACE_COUNT; i++)
        if (FlagGet(FLAG_UNOVA_START + BW_MAP_REPLACE_FLAG + i))
            bits |= 1u << i;
    return bits;
}   // BGMPush / BGMPop

static bool32 PartyHasPokerus(void)
{
    u32 k;
    for (k = 0; k < gPlayerPartyCount; k++)
        if (CheckMonPokerus(&gPlayerParty[k]))
            return TRUE;
    return FALSE;
}

// our type -> BW's numbering (Normal 0 ... Dark 16; BW has no Fairy, and no ??? type)
static u16 BwType(u32 type)
{
    static const u8 sTypes[] = {
        [TYPE_NORMAL] = 0, [TYPE_FIGHTING] = 1, [TYPE_FLYING] = 2, [TYPE_POISON] = 3, [TYPE_GROUND] = 4,
        [TYPE_ROCK] = 5, [TYPE_BUG] = 6, [TYPE_GHOST] = 7, [TYPE_STEEL] = 8, [TYPE_FIRE] = 9, [TYPE_WATER] = 10,
        [TYPE_GRASS] = 11, [TYPE_ELECTRIC] = 12, [TYPE_PSYCHIC] = 13, [TYPE_ICE] = 14, [TYPE_DRAGON] = 15,
        [TYPE_DARK] = 16, [TYPE_FAIRY] = 0,
    };
    return type < ARRAY_COUNT(sTypes) ? sTypes[type] : 0;
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

#include "data/bw_songs.h"
#include "data/bw_item_pockets.h"   // sBwItemPocket: BW item -> its pocket (bw/items.py)
#include "data/bw_demos.h"   // sBwDemoFrames: cutscene id -> its length (bw/demos.py)   // sBwSongs: BW sound id -> MUS_BW_* (import_bw.py)

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
    case BW_WAIT_MENU:
        if (ScriptContext_IsEnabled() == FALSE)
            return TRUE;
        BwVarSet(sVm.menuDest, gSpecialVar_Result == MULTI_B_PRESSED ? 0xFFFE : gSpecialVar_Result);
        break;
    case BW_WAIT_PARTY:
        if (gMain.callback2 != CB2_Overworld)
            return TRUE;
        BwVarSet(sVm.partyDest, gSpecialVar_0x8004 < PARTY_SIZE ? gSpecialVar_0x8004 : 0xFF);
        BwVarSet(sVm.battleDest, gSpecialVar_0x8004 < PARTY_SIZE);
        break;
    case BW_WAIT_SE:
        if (IsSEPlaying())
            return TRUE;
        break;
    case BW_WAIT_DEMO:
        if (Web3D_StepDemo(sVm.demoFrames, &sVm.demoWaited))
            return TRUE;
        break;
    case BW_WAIT_CALL:
        if (RunTextPrintersAndIsPrinter0Active() || !JOY_NEW(A_BUTTON | B_BUTTON))
            return TRUE;
        if (++sVm.callLine < *sBwCalls[sVm.callId].count)
        {
            ShowMessage(sBwCalls[sVm.callId].strings[sVm.callLine]);
            sVm.wait = BW_WAIT_CALL;
            return TRUE;
        }
        HideFieldMessageBox();
        sVm.messageOpen = FALSE;
        break;
    case BW_WAIT_FIELD:
        if (gMain.callback2 != CB2_Overworld)
            return TRUE;
        BwVarSet(sVm.partyDest, 1);
        break;
    case BW_WAIT_PROP:
        if (sVm.waitFrames)
        {
            sVm.waitFrames--;
            return TRUE;
        }
        if (Web3D_PropBusy())
            return TRUE;
        break;
    case BW_WAIT_STARTER:
        if (gMain.callback2 != CB2_Overworld)
            return TRUE;
        BwVarSet(sVm.battleDest, gSpecialVar_Result);
        FlagSet(FLAG_SYS_POKEMON_GET);   // BW gives the menu its POKéMON entry with the starter
        break;
    }
    sVm.wait = BW_WAIT_NONE;
    return FALSE;
}

static void EndScript(void)
{
    BW_TRACE("[bw] end");
#if BW_TRACE_ON
    {
        u32 i;
        for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
            if (gObjectEvents[i].active)
                BW_TRACE("[bw] obj %d at %d,%d facing %d", gObjectEvents[i].localId, gObjectEvents[i].currentCoords.x - MAP_OFFSET, gObjectEvents[i].currentCoords.y - MAP_OFFSET, gObjectEvents[i].facingDirection);
    }
#endif
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
again:
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
        case 0x6C: { u16 a = BwFlex(Read16()); SetActorVisible(a, FALSE); DropCompanion(a); break; }  // ActorDelete
        case 0x241: AddCompanion(BwFlex(Read16())); break;     // the friends walk with you (see AddCompanion)
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
        case 0x8B: // TrainerBGMPlayPush: the trainer's eye-contact theme
        {
            u16 t = BwFlex(Read16());
            if (t && t < UNOVA_TRAINERS_COUNT)
                PlayNewMapMusic(GetUnovaTrainerEyeMusic(UNOVA_TRAINERS_START + t));
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
        case 0xA9: // MEPlay: BW's own fanfare where we converted it (bw/music.py), else ours
        {
            u16 song = BwSong(BwFlex(Read16()));
            if (!song || !PlayBwFanfare(song))
                PlayFanfare(MUS_OBTAIN_ITEM);
            break;
        }
        case 0xA6: // SEPlay: BW's sound effects, on the SE player
        {
            u16 id = BwFlex(Read16()), song = BwSong(id);
            if (song && id != 1351)          // SEQ_SE_MESSAGE: our own talk sound already plays
                PlaySE(song);
            break;
        }
        case 0xA7: m4aMPlayStop(&gMPlayInfo_SE1); m4aMPlayStop(&gMPlayInfo_SE2); break;   // SEStop
        case 0xA8: sVm.wait = BW_WAIT_SE; break;               // SEWait
        case 0x154: // Call3DDemo: BW's 3D cutscenes, played by the page (bw/demos.py, unova3d.js)
        {
            u16 id = BwFlex(Read16()), i;
            Read16();
            for (i = 0; i < ARRAY_COUNT(sBwDemoFrames); i++)
                if (sBwDemoFrames[i][0] == id)
                    break;
            if (i == ARRAY_COUNT(sBwDemoFrames))
                break;
            sVm.demoFrames = sBwDemoFrames[i][1];
            sVm.demoWaited = 0;
            Web3D_PlayDemo(id);
            sVm.wait = BW_WAIT_DEMO;
            break;
        }
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
            if (gPlayerPartyCount)
                FlagSet(FLAG_SYS_POKEMON_GET);
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
                if (gPlayerPartyCount)
                    FlagSet(FLAG_SYS_POKEMON_GET);
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
        case 0x17C: { u16 dest = Read16(); BwVarSet(dest, gBattleOutcome); break; }      // WildBattleGetResult

        // --- cutscenes: BW's event camera (the 3D view eases to it; src/platform/web3d.c)
        case 0x13F: case 0x141: case 0x142: break;             // EVCameraInit, Unbind, Rebind
        case 0x140: // EVCameraEnd: back to the field camera at once
            Web3D_EventCamera(2, 0, 0, 0, 0, 0, 0, 1);
            sVm.cameraFrames = 0;
            break;
        case 0x143: // EVCameraMoveTo: pitch, yaw (fx16 angles), distance, target x, y, z (fx32), frames
        {
            u16 pitch = Read16(), yaw = Read16();
            s32 dist = Read32(), x = Read32(), y = Read32(), z = Read32();
            u16 frames = BwFlex(Read16());
            Web3D_EventCamera(1, pitch, yaw, dist, x, y, z, frames);
            sVm.cameraFrames = frames;
            break;
        }
        case 0x144: case 0x147: // EVCameraReturn, EVCameraMoveToDefault: back to the field camera
        {
            u16 frames = BwFlex(Read16());
            Web3D_EventCamera(2, 0, 0, 0, 0, 0, 0, frames);
            sVm.cameraFrames = frames;
            break;
        }
        case 0x146: Read16(); Read16(); break;                 // EVCameraMoveToCommon: preset views (not known)
        case 0x145: // EVCameraWait
            if (sVm.cameraFrames)
            {
                sVm.waitFrames = sVm.cameraFrames;
                sVm.cameraFrames = 0;
                sVm.wait = BW_WAIT_FRAMES;
            }
            break;

        // --- actors
        case 0x69: // ActorNew: x, z, dir, actor id, objCode, moveCode
        {
            u16 x = BwFlex(Read16()), z = BwFlex(Read16()), dir = BwFlex(Read16()), actor = BwFlex(Read16());
            u16 obj = BwFlex(Read16());
            Read16();
            NewActor(x, z, dir, actor, obj);
            break;
        }
        case 0x6D: // ActorSetGPos: actor, x, y, z, dir
        {
            u16 actor = Read16(), x = BwFlex(Read16()), dir;
            Read16();
            {
                u16 z = BwFlex(Read16());
                dir = BwFlex(Read16());
                SetActorPos(actor, x, z, dir, TRUE);
            }
            break;
        }
        case 0x7B: // ActorJumpToGPos: actor, x, y, z
        {
            u16 actor = Read16(), x = BwFlex(Read16()), z;
            Read16();
            z = BwFlex(Read16());
            SetActorPos(actor, x, z, 0, FALSE);
            break;
        }
        case 0x67: // ActorGetGPos: actor, -> x, z
        {
            u16 actor = Read16(), vx = Read16(), vz = Read16();
            struct ObjectEvent *o = ActorObject(actor);
            const struct BwZoneMap *m = CurrentZoneMap();
            if (o)
            {
                BwVarSet(vx, o->currentCoords.x - MAP_OFFSET + (m ? m->x : 0));
                BwVarSet(vz, o->currentCoords.y - MAP_OFFSET + (m ? m->y : 0));
            }
            break;
        }

        // --- list menus
        case 0xB2: // ListMenuInitTR: x, y, default, cancellable, -> result
            sVm.menuX = min(Read8(), 20);
            sVm.menuY = min(Read8(), 10);
            sVm.menuDefault = BwFlex(Read16());
            sVm.menuCancel = Read8();
            sVm.menuDest = Read16();
            sVm.menuCount = 0;
            break;
        case 0xAF: { u16 msg = Read16(); Read16(); MenuAdd(msg, BwFlex(Read16())); break; }   // ListMenuAdd: text, hint, uid
        case 0xB0: case 0xB1: // ListMenuShow
            if (MenuShow())
                return TRUE;
            break;

        // --- more messages and words
        case 0x3A: { u16 msg = Read16(); Read16(); Read16(); Read16(); ShowMessage(Message(BW_ZONE_TEXT, msg)); break; }  // MsgMulti
        case 0x3B: Read16(); if (sVm.messageOpen) { HideFieldMessageBox(); sVm.messageOpen = FALSE; } break;          // MsgWinCloseNo
        case 0x51: { u8 b = Read8(); u16 m = BwFlex(Read16()); if (m < MOVES_COUNT) StringCopy(Buffer(b), GetMoveName(m)); break; }
        case 0x53: case 0x54: // WordSetPartyPokeSpecies / Name
        {
            u8 b = Read8();
            u16 idx = BwFlex(Read16());
            if (idx < PARTY_SIZE)
            {
                if (op == 0x53)
                    StringCopy(Buffer(b), GetSpeciesName(GetMonData(&gPlayerParty[idx], MON_DATA_SPECIES)));
                else
                {
                    GetMonData(&gPlayerParty[idx], MON_DATA_NICKNAME, Buffer(b));
                    StringGet_Nickname(Buffer(b));
                }
            }
            break;
        }
        case 0x56: { u8 b = Read8(); u16 t = BwFlex(Read16()); if (t + 1 < NUMBER_OF_MON_TYPES) StringCopy(Buffer(b), gTypesInfo[t + 1].name); break; }

        // --- the party
        case 0x103: // PokePartyGetCount: dest, mode (0: all, else: not eggs)
        {
            u16 dest = Read16(), mode = BwFlex(Read16()), n = 0, i;
            for (i = 0; i < PARTY_SIZE; i++)
            {
                u16 sp = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
                if (sp != SPECIES_NONE && (mode == 0 || !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG)))
                    n++;
            }
            BwVarSet(dest, n);
            break;
        }
        case 0xFE: // PokePartyGetSpecies: dest, slot (as the National Dex number)
        {
            u16 dest = Read16(), idx = BwFlex(Read16());
            BwVarSet(dest, idx < PARTY_SIZE ? SpeciesToNationalPokedexNum(GetMonData(&gPlayerParty[idx], MON_DATA_SPECIES)) : 0);
            break;
        }
        case 0x220: // PokePartyFindEx: species, form, -> slot, -> found
        {
            u16 sp = Species(BwFlex(Read16())), i, slot = 0, found = 0;
            u16 vs, vf;
            Read16();
            vs = Read16();
            vf = Read16();
            for (i = 0; i < PARTY_SIZE; i++)
                if (sp && GetMonData(&gPlayerParty[i], MON_DATA_SPECIES) == sp && !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG))
                {
                    slot = i;
                    found = 1;
                    break;
                }
            BwVarSet(vs, slot);
            BwVarSet(vf, found);
            break;
        }
        case 0x116: // PokePartyHasMoveAny: dest, move
        {
            u16 dest = Read16(), mv = BwFlex(Read16()), i, has = 0;
            for (i = 0; i < PARTY_SIZE; i++)
                if (MonKnowsMove(&gPlayerParty[i], mv))
                    has = 1;
            BwVarSet(dest, has);
            break;
        }
        case 0x107: // CallPokeSelect(-> chose one, -> slot, eggs allowed): the order of Black's handler's reads
            sVm.battleDest = Read16();
            sVm.partyDest = Read16();
            Read16();
            if (sVm.messageOpen)
            {
                HideFieldMessageBox();
                sVm.messageOpen = FALSE;
            }
            ChoosePartyMon();
            ScriptContext_Stop();
            sVm.wait = BW_WAIT_PARTY;
            return TRUE;

        // --- money
        case 0xF9: AddMoney(&gSaveBlock1Ptr->money, BwFlex(Read16())); break;
        case 0xFA: RemoveMoney(&gSaveBlock1Ptr->money, BwFlex(Read16())); break;
        case 0xFB: { u16 dest = Read16(); BwVarSet(dest, IsEnoughMoney(&gSaveBlock1Ptr->money, BwFlex(Read16()))); break; }
        case 0x40: Read16(); Read16(); break;                 // MoneyWinDisp / Close / Update: no box yet
        case 0x41: case 0x42: break;

        // --- clock (BW: day parts 0 morning, 1 day, 2 evening, 3 night)
        case 0xCF: BwVarSet(Read16(), GetDayOfWeek()); break;
        case 0xD2: BwVarSet(Read16(), (GetMonth() - MONTH_JAN) % 4); break;
        case 0xCD:
        {
            static const u8 sParts[] = {[TIME_MORNING] = 0, [TIME_DAY] = 1, [TIME_EVENING] = 2, [TIME_NIGHT] = 3};
            u32 t = GetTimeOfDay();
            BwVarSet(Read16(), t < ARRAY_COUNT(sParts) ? sParts[t] : 1);
            break;
        }
        case 0x1C4: { u16 dest = Read16(); Read16(); BwVarSet(dest, 0); break; }     // PokeDexIsComplete

        // --- scripts
        case 0x21: sVm.reserved = Read16(); break;              // RTReserveScript: next, once this one ends

        // --- other warps: like MapChangeWarp
        case 0xBE: case 0xBF: case 0xC1: case 0xC4:
        {
            u16 zone = Read16(), x = Read16(), z;
            if (op == 0xC4)
                Read16();                                       // y
            z = Read16();
            if (op != 0xC1)
                Read16();                                       // dir
            EndScript();
            WarpTo(zone, x, z);
            ResetInitialPlayerAvatarState();
            ScriptContext_Stop();
            return TRUE;
        }

        // ---- Commands BW's list leaves unnamed or undocumented. What each does was checked in Pokémon
        // Black's own handler code (its parameter reads, in order, and what it calls; 2026-11-26); the
        // names are only labels (some borrowed from the B2W2 SDK, whose matching handlers share them).
        // Parameter sizes: bw/param_sizes.py.

        // --- nothing for us to do: sub-event bookkeeping, debug output, the field closing for an
        // app (ours draw over it), wireless, records, interactive-music switches
        case 0x30: break;                                           // RTFinishSubEvents
        case 0x06: Read16(); break;                               // DebugPrint
        case 0x14A: case 0x14B: break;                            // FieldOpen, FieldClose
        case 0xA3: case 0xA4: Read16(); break;                    // ISSSwitchEnable / Disable
        case 0xA5: { u16 dest = Read16(); Read16(); BwVarSet(dest, 0); break; }   // ISSSwitchQuery(-> on, switch)
        case 0x23A: Read16(); Read16(); break;                    // Records.Add
        case 0x25F: break;                                        // BGMAmbienceResume
        case 0x13B: BwVarSet(Read16(), 0); break;                 // GameComm.CheckDSiWiFi(-> on): no
        case 0x13C: case 0x13D: case 0x13E: break;                // a sound state on / off (Black's handlers), comm housekeeping
        case 0x8E: case 0x179: break;                             // EndTrainerBattle, EndWildBattle (ours end themselves)
        case 0x17A: break;                                        // CallWildLose (our battles white out)
        case 0x37: Read16(); break;                               // MsgSetLoadingSpinner
        case 0xA0: break;                                         // BGMWait

        // --- fades: Black's handlers all call one fade with (mode, colour): modes 0/2 in, 1/3 out (0/1 at
        // speed -1, 2/3 at speed 4 - their exact timing is our estimate), colour 0 black, 1 white
        case 0x1AD: case 0x1B2:                                   // InBlackQ
            BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
            break;
        case 0x1AE: BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK); break;   // OutBlackQ
        case 0x1AF: BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_WHITE); break;   // InWhiteQ
        case 0x1B0: BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_WHITE); break;   // OutWhiteQ
        case 0x1B5: BeginNormalPaletteFade(PALETTES_ALL, 2, 16, 0, RGB_BLACK); break;   // InBlack
        case 0x1B6: BeginNormalPaletteFade(PALETTES_ALL, 2, 0, 16, RGB_BLACK); break;   // OutBlack
        case 0x1B7: BeginNormalPaletteFade(PALETTES_ALL, 2, 16, 0, RGB_WHITE); break;   // InWhite
        case 0x1B8: BeginNormalPaletteFade(PALETTES_ALL, 2, 0, 16, RGB_WHITE); break;   // OutWhite
        case 0x1B1: sVm.wait = BW_WAIT_FADE; break;               // Fade.Wait

        // --- music
        case 0x248: // BGMPlayEx: a song, faded in over `interval` frames
        {
            u16 song = BwSong(Read16());
            Read16();
            if (song)
                PlayNewMapMusic(song);
            break;
        }
        case 0x24B: // PlayMapBGMEx: back to the map's music
            Read16();
            PlayNewMapMusic(GetCurrLocationDefaultMusic());
            break;
        case 0xA1: // BGMPush: remember the song (BGMPop brings it back)
            Read16();
            sBwPushedSong = GetCurrentMapMusic();
            break;
        case 0xA2: // BGMPop
            Read16(); Read16();
            PlayNewMapMusic(sBwPushedSong ? sBwPushedSong : GetCurrLocationDefaultMusic());
            sBwPushedSong = 0;
            break;

        // --- the field
        case 0x252: ShowMapNamePopup(); break;                    // Field.CallPlaceNameDisp
        case 0xE3: FlagSet(FLAG_SYS_B_DASH); break;               // PlayerEnableRunningShoes
        case 0xD3: BwVarSet(Read16(), sVm.zone); break;           // FieldGetZoneID
        case 0x22: BwVarSet(Read16(), 0); break;                  // FieldGetContinueFlag: not continuing a link session
        case 0xD8: // MapReplaceIsEventSet(id, -> set)
        {
            u16 id = BwFlex(Read16()), dest = Read16();
            BwVarSet(dest, id < BW_MAP_REPLACE_COUNT && FlagGet(FLAG_UNOVA_START + BW_MAP_REPLACE_FLAG + id));
            break;
        }
        case 0xDA: // MapReplaceSetEvent(id, on, reload): the room after the battle, and so on
        {
            u16 id = BwFlex(Read16()), on = BwFlex(Read16());
            Read16();
            if (id < BW_MAP_REPLACE_COUNT)
            {
                if (on)
                    FlagSet(FLAG_UNOVA_START + BW_MAP_REPLACE_FLAG + id);
                else
                    FlagClear(FLAG_UNOVA_START + BW_MAP_REPLACE_FLAG + id);
                Web3D_MapReplace(id, on);
            }
            break;
        }
        case 0x78: // PlayerGetExState: 0 walking, 1 cycling, 2 surfing
            BwVarSet(Read16(), TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING) ? 2
                             : TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_BIKE) ? 1 : 0);
            break;
        case 0x75: Read16(); break;                               // PlayerSetSpecialSequence (poses)
        case 0x77: break;                                         // PlayerTurnByTrigger

        // --- the clock
        case 0xD0: // RTCGetDate(-> month, -> day)
        {
            u16 m = Read16(), d = Read16();
            BwVarSet(m, GetMonth() - MONTH_JAN + 1);
            BwVarSet(d, GetDay());
            break;
        }
        case 0xD1: // RTCGetTime(-> hour, -> minute)
        {
            u16 h = Read16(), mi = Read16();
            RtcCalcLocalTime();
            BwVarSet(h, gLocalTime.hours);
            BwVarSet(mi, gLocalTime.minutes);
            break;
        }

        // --- the party
        case 0x102: { u16 dest = Read16(), i = BwFlex(Read16()); BwVarSet(dest, i < PARTY_SIZE && GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG)); break; }  // IsEgg
        case 0xFC: { u16 dest = Read16(), i = BwFlex(Read16()); BwVarSet(dest, i < PARTY_SIZE ? GetMonData(&gPlayerParty[i], MON_DATA_FRIENDSHIP) : 0); break; }  // GetHappiness
        case 0xFD: // AdjustHappiness(slot, mode 0 add / 1 subtract, amount)
        {
            u16 i = BwFlex(Read16()), mode = BwFlex(Read16()), n = BwFlex(Read16());
            if (i < PARTY_SIZE)
            {
                s32 f = GetMonData(&gPlayerParty[i], MON_DATA_FRIENDSHIP);
                f = mode ? f - n : f + n;
                f = f < 0 ? 0 : f > 255 ? 255 : f;
                SetMonData(&gPlayerParty[i], MON_DATA_FRIENDSHIP, &f);
            }
            break;
        }
        case 0xFF: { u16 dest = Read16(); Read16(); BwVarSet(dest, 0); break; }   // GetForme (no forms in BW1's story)
        case 0x10A: // GetMove(-> move, slot, move slot)
        {
            u16 dest = Read16(), i = BwFlex(Read16()), m = BwFlex(Read16());
            BwVarSet(dest, i < PARTY_SIZE && m < MAX_MON_MOVES ? GetMonData(&gPlayerParty[i], MON_DATA_MOVE1 + m) : 0);
            break;
        }
        case 0x115: // HasMove(-> knows it, move, slot)
        {
            u16 dest = Read16(), mv = BwFlex(Read16()), i = BwFlex(Read16()), k;
            bool32 has = FALSE;
            for (k = 0; i < PARTY_SIZE && k < MAX_MON_MOVES; k++)
                if (GetMonData(&gPlayerParty[i], MON_DATA_MOVE1 + k) == mv)
                    has = TRUE;
            BwVarSet(dest, has);
            break;
        }
        case 0x108: // GetLearnedMoveCount
        {
            u16 dest = Read16(), i = BwFlex(Read16()), k, n = 0;
            for (k = 0; i < PARTY_SIZE && k < MAX_MON_MOVES; k++)
                if (GetMonData(&gPlayerParty[i], MON_DATA_MOVE1 + k))
                    n++;
            BwVarSet(dest, n);
            break;
        }
        case 0x101: // IsFullHP
        {
            u16 dest = Read16(), i = BwFlex(Read16());
            BwVarSet(dest, i < PARTY_SIZE && GetMonData(&gPlayerParty[i], MON_DATA_HP) == GetMonData(&gPlayerParty[i], MON_DATA_MAX_HP));
            break;
        }
        case 0x100: BwVarSet(Read16(), PartyHasPokerus()); break;            // IsPokerusAny
        case 0x113: { u16 dest = Read16(); Read16(); BwVarSet(dest, 1); break; }               // IsOriginGame
        case 0x112: // GetEVTotal
        {
            u16 dest = Read16(), i = BwFlex(Read16()), k, n = 0;
            for (k = 0; i < PARTY_SIZE && k < NUM_STATS; k++)
                n += GetMonData(&gPlayerParty[i], MON_DATA_HP_EV + k);
            BwVarSet(dest, n);
            break;
        }
        case 0x114: // GetPkmCountBySpecies(species, -> count)
        {
            u16 sp = Species(BwFlex(Read16())), dest = Read16(), k, n = 0;
            for (k = 0; k < gPlayerPartyCount; k++)
                if (!GetMonData(&gPlayerParty[k], MON_DATA_IS_EGG) && GetMonData(&gPlayerParty[k], MON_DATA_SPECIES) == sp)
                    n++;
            BwVarSet(dest, n);
            break;
        }
        case 0x118: // FindPkm(species, -> found, -> slot)
        {
            u16 sp = Species(BwFlex(Read16())), ok = Read16(), slot = Read16(), k;
            BwVarSet(ok, 0);
            for (k = 0; k < gPlayerPartyCount; k++)
            {
                if (!GetMonData(&gPlayerParty[k], MON_DATA_IS_EGG) && GetMonData(&gPlayerParty[k], MON_DATA_SPECIES) == sp)
                {
                    BwVarSet(ok, 1);
                    BwVarSet(slot, k);
                    break;
                }
            }
            break;
        }
        case 0x10D: // GetMemberByType(-> slot, 1 an egg / 2 fainted)
        {
            u16 dest = Read16(), cond = BwFlex(Read16()), k;
            BwVarSet(dest, 0xFF);
            for (k = 0; k < gPlayerPartyCount; k++)
            {
                bool32 egg = GetMonData(&gPlayerParty[k], MON_DATA_IS_EGG);
                if ((cond == 1 && egg) || (cond == 2 && !egg && GetMonData(&gPlayerParty[k], MON_DATA_HP) == 0))
                {
                    BwVarSet(dest, k);
                    break;
                }
            }
            break;
        }
        case 0x11B: // GetTypes(-> type 1, -> type 2, slot): BW's type numbers are the games' usual order
        {
            u16 t1 = Read16(), t2 = Read16(), i = BwFlex(Read16());
            u16 sp = i < PARTY_SIZE ? GetMonData(&gPlayerParty[i], MON_DATA_SPECIES) : SPECIES_NONE;
            BwVarSet(t1, BwType(GetSpeciesType(sp, 0)));
            BwVarSet(t2, BwType(GetSpeciesType(sp, 1)));
            break;
        }
        case 0x11D: Read16(); break;                              // TrainerCard.SetFavePokemon
        case 0x11E: Read16(); break;                              // TrainerCard.SaveGymVictoryParty
        case 0x121: // Box.GetPkmCount(-> count, mode): 4 = free slots, else Pokémon stored
        {
            u16 dest = Read16(), mode = BwFlex(Read16());
            u32 b, s, used = 0;
            for (b = 0; b < TOTAL_BOXES_COUNT; b++)
                for (s = 0; s < IN_BOX_COUNT; s++)
                    if (GetBoxMonDataAt(b, s, MON_DATA_SPECIES))
                        used++;
            BwVarSet(dest, mode == 4 ? TOTAL_BOXES_COUNT * IN_BOX_COUNT - used : used);
            break;
        }

        // --- items
        case 0xBB: // ItemGetPocket(item, -> pocket): Black's own (0 items, 1 medicine, 2 TMs, 3 berries, 4 key items)
        {
            u16 bwItem = BwFlex(Read16()), dest = Read16();
            BwVarSet(dest, bwItem < ARRAY_COUNT(sBwItemPocket) ? sBwItemPocket[bwItem] : 0);
            break;
        }
        case 0xBA: { u16 it = Item(BwFlex(Read16())), dest = Read16(); BwVarSet(dest, it && GetItemPocket(it) == POCKET_TM_HM); break; }   // IsTMHM
        case 0xBD: { Read16(); BwVarSet(Read16(), 0); break; }    // GetClass

        // --- the Pokédex (BW's counts are the Unova dex: Victini to Genesect)
        case 0xDD: // GetDexCount(caught only, -> count)
        {
            u16 caught = BwFlex(Read16()), dest = Read16(), sp, n = 0;
            for (sp = NATIONAL_DEX_VICTINI; sp <= NATIONAL_DEX_GENESECT; sp++)
                if (GetSetPokedexFlag(sp, caught ? FLAG_GET_CAUGHT : FLAG_GET_SEEN))
                    n++;
            BwVarSet(dest, n);
            break;
        }
        case 0xDF: // IsSpeciesRegistered(caught only, species, -> registered)
        {
            u16 caught = BwFlex(Read16()), dex = BwFlex(Read16()), dest = Read16();
            BwVarSet(dest, dex && dex < NATIONAL_DEX_COUNT && GetSetPokedexFlag(dex, caught ? FLAG_GET_CAUGHT : FLAG_GET_SEEN));
            break;
        }
        case 0xDE: // RegisterSpecies(mode, species): seen and caught
        {
            u16 dex;
            Read16();
            dex = BwFlex(Read16());
            if (dex && dex < NATIONAL_DEX_COUNT)
            {
                GetSetPokedexFlag(dex, FLAG_SET_SEEN);
                GetSetPokedexFlag(dex, FLAG_SET_CAUGHT);
            }
            break;
        }

        // --- BW's animated buildings (Black's BM* commands, "static props"): the page plays them
        case 0x127: // CreateHandle(-> handle, type, x, z): a handle is a slot remembering the building
        {
            u16 dest = Read16(), type = BwFlex(Read16()), x = BwFlex(Read16()), z = BwFlex(Read16()), i;
            for (i = 0; i < ARRAY_COUNT(sBwProps) - 1 && sBwProps[i].used; i++)
                ;
            sBwProps[i].used = TRUE;
            sBwProps[i].type = type;
            sBwProps[i].x = x;
            sBwProps[i].z = z;
            BwVarSet(dest, i);
            break;
        }
        case 0x128: { u16 h = BwFlex(Read16()); if (h < ARRAY_COUNT(sBwProps)) sBwProps[h].used = FALSE; break; }   // ReleaseHandle
        case 0x129: case 0x124: // HndPlayAudioVisualAnm / HndPlayAnm(handle, clip)
        {
            u16 h = BwFlex(Read16()), anim = BwFlex(Read16());
            if (h < ARRAY_COUNT(sBwProps))
                Web3D_PropCommand(1, sBwProps[h].type, sBwProps[h].x, sBwProps[h].z, anim);
            break;
        }
        case 0x12C: { u16 h = BwFlex(Read16()); if (h < ARRAY_COUNT(sBwProps)) Web3D_PropCommand(3, sBwProps[h].type, sBwProps[h].x, sBwProps[h].z, 0); break; }  // HndPauseAnm
        case 0x12A: Read16(); sVm.wait = BW_WAIT_PROP; sVm.waitFrames = 2; break;      // HndWaitAnm
        case 0x12E: case 0x12B: // PlayAnm / PlayAnmInv(type, x, z): its first clip, forwards or backwards
        {
            u16 type = BwFlex(Read16()), x = BwFlex(Read16()), z = BwFlex(Read16());
            Web3D_PropCommand(op == 0x12B ? 2 : 1, type, x, z, 0);
            break;
        }
        case 0x12D: Read16(); Read16(); Read16(); Read16(); break;                       // SetVisible (not yet)
        case 0x126: Read16(); Read16(); Read16(); Read16(); break;                       // ChangeMdlID (not yet)
        case 0x125: break;                                                               // PlayHOFMachineSeq

        case 0xCC: BwVarSet(Read16(), Top()->text); break;      // RTGetTextFile: the running script's text file
        case 0x1EC: // ObjInit.SetNPCGPos(npc, dir, x, y, z): where an NPC stands when the map loads
        {
            u16 npc = BwFlex(Read16()), dir = BwFlex(Read16()), x = BwFlex(Read16()), z;
            Read16();
            z = BwFlex(Read16());
            SetActorPos(npc, x, z, dir, TRUE);
            break;
        }
        case 0x1EB: case 0x1EA: Read16(); Read16(); Read16(); Read16(); break;   // ObjInit warp / sign positions (not yet)

        // --- CallPokeNameInput(-> done, party slot, transition): the nickname screen for that Pokémon
        // (Black's handler reads them in that order)
        case 0x105:
        {
            u16 dest = Read16(), slot = BwFlex(Read16());
            Read16();
            if (slot >= gPlayerPartyCount || sVm.immediate)
            {
                BwVarSet(dest, 0);
                break;
            }
            if (sVm.messageOpen)
            {
                HideFieldMessageBox();
                sVm.messageOpen = FALSE;
            }
            sVm.partyDest = dest;
            gSpecialVar_0x8004 = slot;
            ChangePokemonNickname();
            ScriptContext_Stop();
            sVm.wait = BW_WAIT_FIELD;
            return TRUE;
        }

        // --- CallXTransceiver(call): Black's app plays the call (its lines: a/0/0/2 file 191 + call); here
        // they're said in the message box, one after another
        case 0x155:
        {
            u16 id = BwFlex(Read16());
            if (id < ARRAY_COUNT(sBwCalls) && sBwCalls[id].strings && *sBwCalls[id].count && !sVm.immediate)
            {
                sVm.callId = id;
                sVm.callLine = 0;
                PlaySE(SE_POKENAV_CALL);
                ShowMessage(sBwCalls[id].strings[0]);
                sVm.wait = BW_WAIT_CALL;
            }
            break;
        }

        // --- the Pokémon Center: BW's healing jingle (SEQ_ME_ASA, our heal fanfare), and wait for it
        case 0x12F:
            Read16();
            if (!PlayBwFanfare(MUS_BW_ME_ASA))
                PlayFanfare(MUS_HEAL);
            sVm.wait = BW_WAIT_FANFARE;
            break;

        default:
            BW_TRACE("[bw] skip %x at %u", op, f->pc - 2);
            SkipParams(op);
            break;
        }
        if (sVm.wait != BW_WAIT_NONE)
            BW_TRACE("[bw] op %x waits %d", op, sVm.wait);
    }
    if (sVm.reserved && sBwZones[sVm.zone].scripts)
    {
        u16 r = sVm.reserved;
        sVm.reserved = 0;
        sVm.sp = 0;
        if (PushScript(r, sBwZones[sVm.zone].scripts, sBwZones[sVm.zone].text, FALSE))
            goto again;
    }
    EndScript();
    return FALSE;
}

static bool8 BwScript_Native(void)
{
    return Run() == FALSE;
}

// BW's intro ends by flying over Nuvema Town to the player's house (bw/demos.py's INTRO_DEMO):
// a new game in Unova plays it before the room's first scene (new_game.c sets this).
#define BW_INTRO_DEMO 100
EWRAM_DATA bool8 gBwIntroDemoPending = FALSE;

static void StartIntroDemo(void)
{
    u32 i;
    gBwIntroDemoPending = FALSE;
    for (i = 0; i < ARRAY_COUNT(sBwDemoFrames); i++)
    {
        if (sBwDemoFrames[i][0] == BW_INTRO_DEMO)
        {
            sVm.demoFrames = sBwDemoFrames[i][1];
            sVm.demoWaited = 0;
            Web3D_PlayDemo(BW_INTRO_DEMO);
            sVm.wait = BW_WAIT_DEMO;
            return;
        }
    }
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
    BW_TRACE("[bw] start zone %d script %d%s", zone, scrid, immediate ? " (load)" : "");
    if (!immediate)
        SpawnCompanionsHere();
    if (!PushScript(scrid, sBwZones[zone].scripts, sBwZones[zone].text, FALSE))
        return FALSE;
    if (!immediate && gBwIntroDemoPending)
        StartIntroDemo();
    return TRUE;
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
// Saves made before the start flags came from Black's new-game script (2026-11-26) got a guessed
// list: these 105 were set though Black leaves them clear, these 6 clear though it sets them.
static const u16 sOldStartFlagsOnly[] = {1, 505, 512, 513, 514, 522, 563, 564, 565, 569, 570, 571, 580, 623, 627, 628, 629, 630, 631, 632, 633, 634, 635, 638, 642, 643, 644, 645, 646, 649, 650, 651, 670, 671, 672, 673, 674, 689, 690, 698, 709, 710, 711, 712, 713, 714, 715, 716, 720, 721, 722, 723, 724, 725, 726, 727, 728, 732, 733, 749, 752, 753, 754, 755, 756, 761, 764, 765, 777, 778, 779, 781, 782, 783, 784, 785, 786, 787, 788, 789, 790, 791, 792, 793, 794, 795, 801, 802, 810, 818, 819, 821, 822, 823, 824, 825, 826, 827, 828, 830, 831, 832, 833, 834, 840};
static const u16 sNewStartFlagsOnly[] = {519, 529, 616, 618, 2430, 2436};

void BwScript_OnEnterMap(void)
{
    u32 i;
    memset(sDynActors, 0, sizeof(sDynActors));
    memset(sMoveOwner, 0, sizeof(sMoveOwner));
    sCompanionsSpawned = FALSE;
    if (GetRegionForSectionId(gMapHeader.regionMapSectionId) != REGION_UNOVA)
        memset(sCompanions, 0, sizeof(sCompanions));   // ActorNew's actors belong to the map they were added on
    Web3D_EventCamera(0, 0, 0, 0, 0, 0, 0, 1);
    if (GetRegionForSectionId(gMapHeader.regionMapSectionId) != REGION_UNOVA)
        return;
    // saves from before the start flags came from Black's new-game script hid characters Black shows
    // (Cheren and Bianca at the lab - which stopped the story there - Juniper in her lab...): once,
    // if the story hasn't reached the lab yet, put the flags the way Black's new game has them
    if (FlagGet(FLAG_UNOVA_INITIALIZED) && !FlagGet(FLAG_UNOVA_START + BW_START_FLAGS_FIXED_FLAG))
    {
        FlagSet(FLAG_UNOVA_START + BW_START_FLAGS_FIXED_FLAG);
        if (BwVarGet(0x4080) == 0)    // before the lab: nothing yet has set these for the story
        {
            for (i = 0; i < ARRAY_COUNT(sOldStartFlagsOnly); i++)
                if (sOldStartFlagsOnly[i] != 512 && sOldStartFlagsOnly[i] != 514)
                    FlagClear(FLAG_UNOVA_START + sOldStartFlagsOnly[i]);
            for (i = 0; i < ARRAY_COUNT(sNewStartFlagsOnly); i++)
                FlagSet(FLAG_UNOVA_START + sNewStartFlagsOnly[i]);
            // Bianca at the lab (514): there from the start, home once you first leave your house
            // (0x407F), back at the lab after the scene in her house (0x4078)
            if (BwVarGet(0x4078) == 1 || BwVarGet(0x407F) < 1)
                FlagClear(FLAG_UNOVA_START + 514);
        }
    }
    if (FlagGet(FLAG_UNOVA_INITIALIZED))
        return;
    FlagSet(FLAG_UNOVA_START + BW_START_FLAGS_FIXED_FLAG);
    for (i = 0; i < ARRAY_COUNT(sBwStartFlags); i++)
        FlagSet(FLAG_UNOVA_START + sBwStartFlags[i]);
    for (i = 0; i < ARRAY_COUNT(sBwStartVars); i++)
        BwVarSet(sBwStartVars[i][0], sBwStartVars[i][1]);
    FlagSet(FLAG_UNOVA_INITIALIZED);
}
