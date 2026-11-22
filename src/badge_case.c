#include "global.h"
#include "badge_case.h"
#include "bg.h"
#include "decompress.h"
#include "gpu_regs.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon_world.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/songs.h"

// Pokémon World: the Badge Case. One row per region with its 8 badges;
// badges you haven't earned yet show as dark outlines.
//
// Badge art: Hoenn from Emerald, Kanto from FireRed, Sinnoh from the Sinnoh
// project, Johto from Pokémon Heart & Soul (see PROJECT_CREDITS.md).

enum { BADGE_ROW_KANTO, BADGE_ROW_JOHTO, BADGE_ROW_HOENN, BADGE_ROW_SINNOH, BADGE_ROW_COUNT };

static const u8 sRowRegions[BADGE_ROW_COUNT] = {REGION_KANTO, REGION_JOHTO, REGION_HOENN, REGION_SINNOH};

static const u8 *const sRowNames[BADGE_ROW_COUNT] =
{
    COMPOUND_STRING("KANTO"),
    COMPOUND_STRING("JOHTO"),
    COMPOUND_STRING("HOENN"),
    COMPOUND_STRING("SINNOH"),
};

// Each sheet is 128x16: eight 16x16 badges side by side.
static const u32 sBadgeTiles_Kanto[]  = INCGFX_U32("graphics/badge_case/kanto.png", ".4bpp");
static const u32 sBadgeTiles_Johto[]  = INCGFX_U32("graphics/badge_case/johto.png", ".4bpp");
static const u32 sBadgeTiles_Hoenn[]  = INCGFX_U32("graphics/badge_case/hoenn.png", ".4bpp");
static const u32 sBadgeTiles_Sinnoh[] = INCGFX_U32("graphics/badge_case/sinnoh.png", ".4bpp");
static const u16 sBadgePal_Kanto[]  = INCGFX_U16("graphics/badge_case/kanto.png", ".gbapal");
static const u16 sBadgePal_Johto[]  = INCGFX_U16("graphics/badge_case/johto.png", ".gbapal");
static const u16 sBadgePal_Hoenn[]  = INCGFX_U16("graphics/badge_case/hoenn.png", ".gbapal");
static const u16 sBadgePal_Sinnoh[] = INCGFX_U16("graphics/badge_case/sinnoh.png", ".gbapal");

static const u32 *const sBadgeTiles[BADGE_ROW_COUNT] = {sBadgeTiles_Kanto, sBadgeTiles_Johto, sBadgeTiles_Hoenn, sBadgeTiles_Sinnoh};
static const u16 *const sBadgePals[BADGE_ROW_COUNT] = {sBadgePal_Kanto, sBadgePal_Johto, sBadgePal_Hoenn, sBadgePal_Sinnoh};

#define TILES_PER_SHEET   32        // 16 tiles wide x 2 tiles tall
#define SHEET_SIZE        (TILES_PER_SHEET * TILE_SIZE_4BPP)
#define PAL_EARNED(row)   (1 + (row))
#define PAL_MISSING(row)  (1 + BADGE_ROW_COUNT + (row))
#define PAL_TEXT          15

// Layout, in 8x8 tiles.
#define BADGE_X           6         // first badge column
#define BADGE_STEP_X      3         // 16px badge + 8px gap
#define ROW_Y(row)        (3 + (row) * 4)

#define BG_COLOR          RGB(4, 6, 12)
#define MISSING_COLOR     RGB(9, 11, 17)

enum { WIN_TEXT };

static const struct BgTemplate sBgTemplates[] =
{
    { .bg = 0, .charBaseIndex = 1, .mapBaseIndex = 31, .screenSize = 0, .paletteMode = 0, .priority = 0, .baseTile = 0 },
    { .bg = 1, .charBaseIndex = 0, .mapBaseIndex = 30, .screenSize = 0, .paletteMode = 0, .priority = 1, .baseTile = 0 },
};

static const struct WindowTemplate sWindowTemplates[] =
{
    [WIN_TEXT] = { .bg = 0, .tilemapLeft = 0, .tilemapTop = 0, .width = 30, .height = 20, .paletteNum = PAL_TEXT, .baseBlock = 1 },
    DUMMY_WIN_TEMPLATE,
};

static EWRAM_DATA MainCallback sExitCallback = NULL;
static EWRAM_DATA u16 *sTilemap = NULL;

static void MainCB2(void);
static void VBlankCB(void);
static void Task_WaitFadeIn(u8 taskId);
static void Task_WaitForInput(u8 taskId);
static void Task_WaitFadeOut(u8 taskId);

void ShowBadgeCase(MainCallback exitCallback)
{
    sExitCallback = exitCallback;
    SetMainCallback2(CB2_ShowBadgeCase);
}

static void LoadBadgeGraphics(void)
{
    u32 row, i;
    u16 missing[16];

    for (row = 0; row < BADGE_ROW_COUNT; row++)
    {
        LoadBgTiles(1, sBadgeTiles[row], SHEET_SIZE, row * TILES_PER_SHEET);
        LoadPalette(sBadgePals[row], BG_PLTT_ID(PAL_EARNED(row)), PLTT_SIZE_4BPP);

        // Badges not yet earned: the same shape in one flat color.
        missing[0] = 0;
        for (i = 1; i < 16; i++)
            missing[i] = MISSING_COLOR;
        LoadPalette(missing, BG_PLTT_ID(PAL_MISSING(row)), PLTT_SIZE_4BPP);
    }
}

static void DrawBadges(void)
{
    u32 row, badge;

    for (row = 0; row < BADGE_ROW_COUNT; row++)
    {
        for (badge = 0; badge < PW_BADGES_PER_REGION; badge++)
        {
            u32 x = BADGE_X + badge * BADGE_STEP_X;
            u32 y = ROW_Y(row);
            u32 firstTile = row * TILES_PER_SHEET + badge * 2;
            u32 pal = PW_HasBadge(sRowRegions[row], badge) ? PAL_EARNED(row) : PAL_MISSING(row);

            sTilemap[y * 32 + x]           = (firstTile)      | (pal << 12);
            sTilemap[y * 32 + x + 1]       = (firstTile + 1)  | (pal << 12);
            sTilemap[(y + 1) * 32 + x]     = (firstTile + 16) | (pal << 12);
            sTilemap[(y + 1) * 32 + x + 1] = (firstTile + 17) | (pal << 12);
        }
    }
    CopyBgTilemapBufferToVram(1);
}

static void DrawText(void)
{
    static const u8 sColor[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY};
    u32 row;
    u8 *end;

    FillWindowPixelBuffer(WIN_TEXT, PIXEL_FILL(0));
    AddTextPrinterParameterized4(WIN_TEXT, FONT_NORMAL, 8, 4, 0, 0, sColor, TEXT_SKIP_DRAW, COMPOUND_STRING("BADGE CASE"));
    for (row = 0; row < BADGE_ROW_COUNT; row++)
    {
        u32 y = ROW_Y(row) * 8 - 3;

        AddTextPrinterParameterized4(WIN_TEXT, FONT_NORMAL, 4, y, 0, 0, sColor, TEXT_SKIP_DRAW, sRowNames[row]);
        end = ConvertIntToDecimalStringN(gStringVar1, PW_CountBadges(sRowRegions[row]), STR_CONV_MODE_LEFT_ALIGN, 1);
        StringCopy(end, COMPOUND_STRING("/8"));
        AddTextPrinterParameterized4(WIN_TEXT, FONT_SMALL, 4, y + 12, 0, 0, sColor, TEXT_SKIP_DRAW, gStringVar1);
    }
    PutWindowTilemap(WIN_TEXT);
    CopyWindowToVram(WIN_TEXT, COPYWIN_FULL);
}

void CB2_ShowBadgeCase(void)
{
    SetVBlankCallback(NULL);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0);
    SetGpuReg(REG_OFFSET_BG3CNT, 0);
    SetGpuReg(REG_OFFSET_BG2CNT, 0);
    SetGpuReg(REG_OFFSET_BG1CNT, 0);
    SetGpuReg(REG_OFFSET_BG0CNT, 0);
    SetGpuReg(REG_OFFSET_BG1HOFS, 0);
    SetGpuReg(REG_OFFSET_BG1VOFS, 0);
    SetGpuReg(REG_OFFSET_BG0HOFS, 0);
    SetGpuReg(REG_OFFSET_BG0VOFS, 0);
    DmaFill16(3, 0, VRAM, VRAM_SIZE);
    DmaFill32(3, 0, OAM, OAM_SIZE);
    DmaFill16(3, 0, PLTT, PLTT_SIZE);
    ScanlineEffect_Stop();
    ResetTasks();
    ResetSpriteData();
    ResetPaletteFade();
    FreeAllSpritePalettes();

    sTilemap = AllocZeroed(BG_SCREEN_SIZE);
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
    SetBgTilemapBuffer(1, sTilemap);
    InitWindows(sWindowTemplates);
    DeactivateAllTextPrinters();

    LoadPalette(gStandardMenuPalette, BG_PLTT_ID(PAL_TEXT), PLTT_SIZE_4BPP);
    {
        u16 backdrop = BG_COLOR;
        LoadPalette(&backdrop, BG_PLTT_ID(0), PLTT_SIZEOF(1));
    }
    LoadBadgeGraphics();
    DrawBadges();
    DrawText();

    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    ShowBg(0);
    ShowBg(1);
    BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
    EnableInterrupts(INTR_FLAG_VBLANK);
    SetVBlankCallback(VBlankCB);
    SetMainCallback2(MainCB2);
    CreateTask(Task_WaitFadeIn, 0);
}

static void MainCB2(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    UpdatePaletteFade();
}

static void VBlankCB(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void Task_WaitFadeIn(u8 taskId)
{
    if (!gPaletteFade.active)
        gTasks[taskId].func = Task_WaitForInput;
}

static void Task_WaitForInput(u8 taskId)
{
    if (JOY_NEW(A_BUTTON | B_BUTTON))
    {
        PlaySE(SE_SELECT);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
        gTasks[taskId].func = Task_WaitFadeOut;
    }
}

static void Task_WaitFadeOut(u8 taskId)
{
    if (!gPaletteFade.active)
    {
        Free(sTilemap);
        sTilemap = NULL;
        FreeAllWindowBuffers();
        DestroyTask(taskId);
        SetMainCallback2(sExitCallback != NULL ? sExitCallback : CB2_ReturnToFieldFadeFromBlack);
    }
}
