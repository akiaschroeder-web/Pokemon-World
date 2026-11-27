// Pokémon World web build: runs the game in the browser.
//
// The game runs its normal main loop (AgbMain). Once per frame it calls
// VBlankIntrWait(); here that draws the frame, runs the V-blank interrupt, saves if
// needed and then hands control back to the browser until the next frame is due
// (Emscripten's Asyncify turns the wait into a pause instead of a busy loop).
//
// Video: src/platform/gba_fast_draw.c renders the GBA picture into a buffer that the
// page copies onto a canvas (in Unova on top of the 3D map, src/platform/web3d.c). Sound: the V-blank interrupt runs the sound driver, which mixes
// one frame of sound (src/platform/sound_mixer.c); that goes to the page, which plays it
// through Web Audio. Input comes from the keyboard or on-screen buttons
// (platform/web/pokeworld.js). Saves are kept in the browser (localStorage).

#ifdef PLATFORM_WEB

#include <emscripten.h>
#include <string.h>
#include "global.h"
#include "main.h"
#include "siirtc.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/sound.h"
#include "platform/web3d.h"

extern IntrFunc gIntrTable[];
extern unsigned char gPlatformFlash[];
void AgbMain(void);

#define FLASH_SIZE 0x20000


static u16 sGbaFrame[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u32 sRgbaFrame[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static u16 sKeys;
static bool32 sSaveDirty;
static u32 sFramesSinceSaveWrite;

// --- JavaScript side (platform/web/pokeworld.js)

EM_ASYNC_JS(void, Web_WaitForNextFrame, (void), {
    await Module.pwWaitForNextFrame();
});

EM_JS(void, Web_PresentFrame, (const void *rgba), {
    Module.pwPresentFrame(rgba);
});

EM_JS(void, Web_LoadSave, (void *dest, int size), {
    Module.pwLoadSave(dest, size);
});

EM_JS(void, Web_StoreSave, (const void *src, int size), {
    Module.pwStoreSave(src, size);
});

// One V-blank's worth of sound (stereo float samples, interleaved).
EM_JS(void, Web_QueueAudio, (const float *samples, int count, int rate), {
    Module.pwQueueAudio && Module.pwQueueAudio(samples, count, rate);
});

EM_JS(void, Web_Reload, (void), {
    Module.pwReload();
});

EM_JS(void, Web_GetDate, (int *out), {
    var d = new Date();
    HEAP32[(out >> 2) + 0] = d.getFullYear();
    HEAP32[(out >> 2) + 1] = d.getMonth() + 1;
    HEAP32[(out >> 2) + 2] = d.getDate();
    HEAP32[(out >> 2) + 3] = d.getDay();
    HEAP32[(out >> 2) + 4] = d.getHours();
    HEAP32[(out >> 2) + 5] = d.getMinutes();
    HEAP32[(out >> 2) + 6] = d.getSeconds();
});

// Called by the page.
EMSCRIPTEN_KEEPALIVE void Web_SetKeys(int keys)
{
    sKeys = keys & KEYS_MASK;
}

EMSCRIPTEN_KEEPALIVE void *Web_GetFramebuffer(void)
{
    return sRgbaFrame;
}

// Debugging: print what the game is doing (main callbacks and running tasks).
// Function pointers print as table indices; tools/web/wasm_where.py --table names them.
#include "task.h"
EMSCRIPTEN_KEEPALIVE void Web_DebugDump(void)
{
    u32 i;
    printf("callback1=%u callback2=%u vblank=%u state=%u\n", (u32)(uintptr_t)gMain.callback1,
           (u32)(uintptr_t)gMain.callback2, (u32)(uintptr_t)gMain.vblankCallback, gMain.state);
    for (i = 0; i < NUM_TASKS; i++)
        if (gTasks[i].isActive)
            printf("task %u: func=%u data0=%d data1=%d\n", i, (u32)(uintptr_t)gTasks[i].func, gTasks[i].data[0], gTasks[i].data[1]);
}

// Debugging: where the hardware memory is, so the page's console can look at it
// (Module.HEAPU8 at these addresses): IO registers, palette, VRAM, OAM, gMain, gTasks.
EMSCRIPTEN_KEEPALIVE const u32 *Web_DebugAddresses(void)
{
    extern unsigned char gPlatformIoRegs[], gPlatformPltt[], gPlatformVram[], gPlatformOam[];
    static u32 sAddrs[6];
    sAddrs[0] = (uintptr_t)gPlatformIoRegs;
    sAddrs[1] = (uintptr_t)gPlatformPltt;
    sAddrs[2] = (uintptr_t)gPlatformVram;
    sAddrs[3] = (uintptr_t)gPlatformOam;
    sAddrs[4] = (uintptr_t)&gMain;
    sAddrs[5] = (uintptr_t)gTasks;
    return sAddrs;
}

// Debugging (the test harness): where the player is and what blocks each direction.
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "script.h"
EMSCRIPTEN_KEEPALIVE void Web_DebugPlayer(void)
{
    struct ObjectEvent *p = &gObjectEvents[gPlayerAvatar.objectEventId];
    emscripten_log(1, "[player] map %d.%d at %d,%d elev %d/%d facing %d locked %d frozen %d script %d coll S%d N%d W%d E%d",
        gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
        p->currentCoords.x - 7, p->currentCoords.y - 7, p->currentElevation, p->previousElevation, p->facingDirection,
        ArePlayerFieldControlsLocked(), p->frozen, ScriptContext_IsEnabled(),
        GetCollisionInDirection(p, DIR_SOUTH), GetCollisionInDirection(p, DIR_NORTH),
        GetCollisionInDirection(p, DIR_WEST), GetCollisionInDirection(p, DIR_EAST));
}

// The harness: set a variable / flag (testing story scenes without replaying the story). Only memory
// writes: calling game code that starts tasks or warps from the page, mid-frame, crashes the web build.
#include "event_data.h"

EMSCRIPTEN_KEEPALIVE void Web_DebugSetVar(int var, int value)
{
    VarSet(var, value);
}

// a save, made by the game itself at its next frame on the map (see the frame loop)
#include "save.h"
#include "overworld.h"
static bool8 sSaveRequested;
EMSCRIPTEN_KEEPALIVE void Web_DebugRequestSave(void)
{
    sSaveRequested = TRUE;
}

EMSCRIPTEN_KEEPALIVE void Web_DebugSetFlag(int flag, int on)
{
    if (on)
        FlagSet(flag);
    else
        FlagClear(flag);
}

// The harness: 1 while a script or message has the player (it presses A until this is 0).
EMSCRIPTEN_KEEPALIVE int Web_PlayerBusy(void)
{
    return ArePlayerFieldControlsLocked() || ScriptContext_IsEnabled();
}

unsigned int gWebTrace;
EMSCRIPTEN_KEEPALIVE void Web_SetTrace(int on)
{
    gWebTrace = on;
}

// Debugging: windows, BG tilemap buffers and the heap's blocks.
#include "window.h"
#include "bg.h"
#include "malloc.h"
#include "gpu_regs.h"
EMSCRIPTEN_KEEPALIVE void Web_DebugWindows(void)
{
    struct MemBlock *b = (struct MemBlock *)gHeap;
    u32 i, n = 0;
    for (i = 0; i < NUM_BACKGROUNDS; i++)
        printf("bg%u: tilemapBuf=%#x winBuf=%#x metric=%#x type=%u mode=%u cnt=%#x\n", i, (u32)(uintptr_t)GetBgTilemapBuffer(i),
               (u32)(uintptr_t)gWindowBgTilemapBuffers[i], GetBgAttribute(i, BG_ATTR_METRIC), GetBgAttribute(i, BG_ATTR_TYPE), GetBgMode(),
               GetGpuReg(REG_OFFSET_BG0CNT + 2 * i));
    for (i = 0; i < WINDOWS_MAX; i++)
        if (gWindows[i].window.bg != 0xFF)
            printf("win%u: bg=%u x=%u y=%u w=%u h=%u base=%u tileData=%#x\n", i, gWindows[i].window.bg, gWindows[i].window.tilemapLeft,
                   gWindows[i].window.tilemapTop, gWindows[i].window.width, gWindows[i].window.height, gWindows[i].window.baseBlock,
                   (u32)(uintptr_t)gWindows[i].tileData);
    printf("heap at %#x\n", (u32)(uintptr_t)gHeap);
    do
    {
        printf("block %#x alloc=%u size=%#x magic=%#x\n", (u32)(uintptr_t)b->data, b->allocated, b->size, b->magic);
        b = b->next;
    } while (b != (struct MemBlock *)gHeap && ++n < 64);
}

// Sound, for debugging and tests: what the sound driver mixed during the last V-blank.
// Returns {samples pointer (stereo float), number of stereo samples, sample rate, frames mixed,
// frames with a cry playing, the cry's loudness × 1000000, Direct Sound channels on, Game Boy channels on}.
static u32 sAudioInfo[8];
EMSCRIPTEN_KEEPALIVE const u32 *Web_GetAudio(void)
{
    sAudioInfo[3] = gPlatformSoundDebug.frames;
    sAudioInfo[4] = gPlatformSoundDebug.cryFrames;
    sAudioInfo[5] = (u32)(gPlatformSoundDebug.cryRms * 1000000.0f);
    sAudioInfo[6] = gPlatformSoundDebug.activeChannels;
    sAudioInfo[7] = gPlatformSoundDebug.activeCgbChannels;
    return sAudioInfo;
}

static void SendAudio(void)
{
    const float *samples;
    u32 n = PlatformSound_TakeQueued(&samples);

    sAudioInfo[0] = (uintptr_t)samples;
    sAudioInfo[1] = n;
    sAudioInfo[2] = (u32)PLATFORM_SOUND_RATE;
    if (n == 0)
        return; // the sound driver didn't run (not set up yet, or switched off)
    Web_QueueAudio(samples, n, (int)PLATFORM_SOUND_RATE);
}

// --- frame loop

static void ConvertFrame(void)
{
    u32 i;
    bool32 see3D = Web3D_IsActive();
    for (i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        u16 c = sGbaFrame[i];
        if (see3D && !(c & 0x8000))
        {
            sRgbaFrame[i] = 0; // nothing drawn here: the 3D map shows through (src/platform/web3d.c)
            continue;
        }
        u32 r = (c & 0x1F) * 255 / 31;
        u32 g = ((c >> 5) & 0x1F) * 255 / 31;
        u32 b = ((c >> 10) & 0x1F) * 255 / 31;
        sRgbaFrame[i] = 0xFF000000 | (b << 16) | (g << 8) | r; // RGBA bytes in memory order
    }
}

void VBlankIntrWait(void)
{
    // Draw the picture (this runs the H-blank and V-count interrupts line by line).
    memset(sGbaFrame, 0, sizeof(sGbaFrame));
    Web3D_EndFrame(); // Unova: what the 3D view shows, and which sprites it takes from the 2D picture
    DrawFrame(sGbaFrame);
    ConvertFrame();
    Web_PresentFrame(sRgbaFrame);

    // V-blank
    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    REG_DISPSTAT |= INTR_FLAG_VBLANK;
    RunDMAs(DMA_VBLANK);
    if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR)
        gIntrTable[4]();
    REG_DISPSTAT &= ~INTR_FLAG_VBLANK;

    // The V-blank interrupt ran the sound driver (m4aSoundMain): pass the frame's sound on.
    SendAudio();

    // Saving: write to the browser a moment after the game stops writing.
    if (sSaveDirty && ++sFramesSinceSaveWrite > 10)
    {
        Web_StoreSave(gPlatformFlash, FLASH_SIZE);
        sSaveDirty = FALSE;
    }

    Web_WaitForNextFrame();
    REG_KEYINPUT = ~sKeys & KEYS_MASK;

    // the test harness asked for a save (Web_DebugRequestSave): here, between frames, on the map
    if (sSaveRequested && gMain.callback2 == CB2_Overworld && !ScriptContext_IsEnabled())
    {
        sSaveRequested = FALSE;
        TrySavingData(SAVE_NORMAL);
    }

    // Timers: the game reads them to seed its random numbers (e.g. the trainer ID),
    // so give them values that differ from run to run.
    REG_TM1CNT_L = (u16)(emscripten_random() * 0x10000);
    REG_TM2CNT_L = (u16)(emscripten_random() * 0x10000);
    REG_TM3CNT_L = (u16)(emscripten_random() * 0x10000);
}

void Web_MarkSaveDirty(void)
{
    sSaveDirty = TRUE;
    sFramesSinceSaveWrite = 0;
}

int main(void)
{
    memset(gPlatformFlash, 0xFF, FLASH_SIZE);
    Web_LoadSave(gPlatformFlash, FLASH_SIZE);
    REG_KEYINPUT = KEYS_MASK;
    REG_VCOUNT = DISPLAY_HEIGHT + 1;
    AgbMain();
    return 0;
}

// --- BIOS calls that have to do with the system rather than with data

void SoftReset(u32 resetFlags)
{
    if (sSaveDirty)
        Web_StoreSave(gPlatformFlash, FLASH_SIZE);
    Web_Reload();
    for (;;)
        Web_WaitForNextFrame();
}

void RegisterRamReset(u32 resetFlags)
{
}

u32 IntrMain[1]; // the GBA's interrupt dispatcher (src/crt0.s); interrupts are called directly here

int MultiBoot(struct MultiBootParam *mp)
{
    return 1; // no link cable
}

s32 Div(s32 num, s32 denom)
{
    return denom ? num / denom : 0;
}

// --- real-time clock: the browser's local time

#define TO_BCD(n) ((((n) / 10) << 4) | ((n) % 10))

static u8 sRtcStatus = SIIRTCINFO_24HOUR;

void SiiRtcUnprotect(void) {}
void SiiRtcProtect(void) {}

u8 SiiRtcProbe(void)
{
    return 1;
}

bool8 SiiRtcReset(void)
{
    sRtcStatus = SIIRTCINFO_24HOUR;
    return TRUE;
}

bool8 SiiRtcGetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = sRtcStatus;
    return TRUE;
}

bool8 SiiRtcSetStatus(struct SiiRtcInfo *rtc)
{
    sRtcStatus = rtc->status | SIIRTCINFO_24HOUR;
    return TRUE;
}

bool8 SiiRtcGetDateTime(struct SiiRtcInfo *rtc)
{
    int d[7];
    Web_GetDate(d);
    rtc->year = TO_BCD(d[0] % 100);
    rtc->month = TO_BCD(d[1]);
    rtc->day = TO_BCD(d[2]);
    rtc->dayOfWeek = TO_BCD(d[3]);
    rtc->hour = TO_BCD(d[4]);
    rtc->minute = TO_BCD(d[5]);
    rtc->second = TO_BCD(d[6]);
    return TRUE;
}

bool8 SiiRtcGetTime(struct SiiRtcInfo *rtc)
{
    struct SiiRtcInfo full;
    SiiRtcGetDateTime(&full);
    rtc->hour = full.hour;
    rtc->minute = full.minute;
    rtc->second = full.second;
    return TRUE;
}

bool8 SiiRtcSetDateTime(struct SiiRtcInfo *rtc) { return TRUE; }
bool8 SiiRtcSetTime(struct SiiRtcInfo *rtc) { return TRUE; }
bool8 SiiRtcSetAlarm(struct SiiRtcInfo *rtc) { return TRUE; }

#endif // PLATFORM_WEB
