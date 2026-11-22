// Pokémon World web build: runs the game in the browser.
//
// The game runs its normal main loop (AgbMain). Once per frame it calls
// VBlankIntrWait(); here that draws the frame, runs the V-blank interrupt, saves if
// needed and then hands control back to the browser until the next frame is due
// (Emscripten's Asyncify turns the wait into a pause instead of a busy loop).
//
// Video: src/platform/gba_fast_draw.c renders the GBA picture into a buffer that the
// page copies onto a canvas. Input comes from the keyboard or on-screen buttons
// (platform/web/pokeworld.js). Saves are kept in the browser (localStorage).

#ifdef PLATFORM_WEB

#include <emscripten.h>
#include <string.h>
#include "global.h"
#include "main.h"
#include "siirtc.h"
#include "platform/dma.h"
#include "platform/framedraw.h"

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

EMSCRIPTEN_KEEPALIVE void *Web_GetAudio(void)
{
    return NULL; // sound comes later
}

// --- frame loop

static void ConvertFrame(void)
{
    u32 i;
    for (i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        u16 c = sGbaFrame[i];
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

    // Saving: write to the browser a moment after the game stops writing.
    if (sSaveDirty && ++sFramesSinceSaveWrite > 10)
    {
        Web_StoreSave(gPlatformFlash, FLASH_SIZE);
        sSaveDirty = FALSE;
    }

    Web_WaitForNextFrame();
    REG_KEYINPUT = ~sKeys & KEYS_MASK;

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

void IntrMain(void)
{
}

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
