// Pokémon World: Pokémon Black's new-game introduction - Professor Juniper's speech - for a game
// that starts in Unova. Its graphics and text come from Black (tools/pokemon_world/bw/intro.py):
// Juniper's sprite and her idle animation (tossing her Poké Ball), the white-over-grey background
// that lights up, Minccino, Cheren and Bianca. BW's order: welcome, her name, Pokémon, a little about
// yourself, your name, your two friends, the important Pokémon, "Let's go visit the world of
// Pokémon!"; then a fade to white, and the game starts with the flight over Nuvema Town
// (bw_script.c's intro demo). BW's boy/girl question is left out: our setup has chosen the trainer.
#include "global.h"
#include "bg.h"
#include "gpu_regs.h"
#include "juniper_intro.h"
#include "main.h"
#include "menu.h"
#include "naming_screen.h"
#include "new_game.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "scanline_effect.h"
#include "sound.h"
#include "sprite.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "trainer_pokemon_sprites.h"
#include "trainer.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/trainers.h"

#include "../graphics/unova/intro/juniper_frames.h"
#include "../graphics/unova/intro/juniper_text.h"

static const u16 sJuniperPal[] = INCBIN_U16("graphics/unova/intro/juniper.pal");
static const u32 sBgTiles[] = INCBIN_U32("graphics/unova/intro/bg.4bpp");
static const u16 sBgPal[] = INCBIN_U16("graphics/unova/intro/bg.pal");
static const u16 sBgMap[] = INCBIN_U16("graphics/unova/intro/bg_map.bin");

#define INTRO_MUSIC   MUS_BW_STARTING
#define BG_TEXT       0
#define BG_BACK       3
#define WIN_TEXT      0
#define DIALOG_TILE   0x1F0
#define TAG_JUNIPER   0x5A10
#define JUNIPER_X     120
#define JUNIPER_Y     72          // the sprite's centre: 64x128, feet at the text box
#define BACK_START_Y  100         // the background starts dark (scrolled down) and lights up to 0
#define FRAME_BYTES   (64 * 128 / 2)

enum
{
    JI_FADE_IN,
    JI_LIGHT_UP,
    JI_SAY,          // print sScript[step]'s text, then do its action
    JI_WAIT_TEXT,
    JI_WAIT_KEY,
    JI_NAMING_FADE,
    JI_END_FADE,
    JI_EXIT,
};

// what happens before a line is said
enum
{
    ACT_NONE,
    ACT_MINCCINO_IN,     // Minccino appears beside her (with its cry)
    ACT_MINCCINO_OUT,
    ACT_NAME,            // after the line: the naming screen
    ACT_CHEREN,          // Cheren's picture beside her
    ACT_BIANCA,          // Bianca's, on the other side
    ACT_FRIENDS_OUT,
    ACT_END,             // after the line: fade to white, the game starts
};

static const struct { const u8 *text; u8 before; u8 after; } sScript[] =
{
    {sJuniperText_6,  ACT_NONE,         ACT_NONE},
    {sJuniperText_7,  ACT_NONE,         ACT_NONE},
    {sJuniperText_8,  ACT_MINCCINO_IN,  ACT_NONE},
    {sJuniperText_9,  ACT_NONE,         ACT_NONE},
    {sJuniperText_10, ACT_MINCCINO_OUT, ACT_NONE},
    {sJuniperText_16, ACT_NONE,         ACT_NAME},
    {sJuniperText_21, ACT_NONE,         ACT_NONE},
    {sJuniperText_22, ACT_NONE,         ACT_NONE},
    {sJuniperText_23, ACT_CHEREN,       ACT_NONE},
    {sJuniperText_24, ACT_BIANCA,       ACT_NONE},
    {sJuniperText_25, ACT_FRIENDS_OUT,  ACT_NONE},
    {sJuniperText_26, ACT_NONE,         ACT_END},
};
#define STEP_AFTER_NAME 6

static const struct BgTemplate sBgTemplates[] =
{
    {.bg = BG_TEXT, .charBaseIndex = 0, .mapBaseIndex = 30, .priority = 0},
    {.bg = BG_BACK, .charBaseIndex = 2, .mapBaseIndex = 31, .priority = 3},
};

static const struct WindowTemplate sWindowTemplates[] =
{
    [WIN_TEXT] = {.bg = BG_TEXT, .tilemapLeft = 2, .tilemapTop = 15, .width = 26, .height = 4, .paletteNum = 15, .baseBlock = 1},
    DUMMY_WIN_TEMPLATE
};

// Juniper: 64x128, as 8 32x32 parts (the converter wrote the frames in that order)
static const struct OamData sJuniperOam = {.shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .priority = 1};
static const struct Subsprite sJuniperParts[] =
{
    {.x = -32, .y = -64, .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 0, .priority = 1},
    {.x = 0,   .y = -64, .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 16, .priority = 1},
    {.x = -32, .y = -32, .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 32, .priority = 1},
    {.x = 0,   .y = -32, .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 48, .priority = 1},
    {.x = -32, .y = 0,   .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 64, .priority = 1},
    {.x = 0,   .y = 0,   .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 80, .priority = 1},
    {.x = -32, .y = 32,  .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 96, .priority = 1},
    {.x = 0,   .y = 32,  .shape = SPRITE_SHAPE(32x32), .size = SPRITE_SIZE(32x32), .tileOffset = 112, .priority = 1},
};
static const struct SubspriteTable sJuniperSubsprites[] = {{ARRAY_COUNT(sJuniperParts), sJuniperParts}};
static const struct SpriteTemplate sJuniperTemplate =
{
    .tileTag = TAG_JUNIPER, .paletteTag = TAG_JUNIPER, .oam = &sJuniperOam, .anims = gDummySpriteAnimTable,
    .images = NULL, .affineAnims = gDummySpriteAffineAnimTable, .callback = SpriteCallbackDummy,
};

static EWRAM_DATA struct
{
    u8 state;
    u8 step;
    u16 timer;
    s16 backY;
    u8 juniper;          // sprite ids (0xFF = none)
    u8 minccino;
    u8 cheren;
    u8 bianca;
    u8 idleStep;         // where her idle animation is (sJuniperIdle), and its time left (BW ticks)
    u16 idleTicks;
    const u32 *pendingFrame;
} sJI = {0};

static void CB2_JuniperIntro(void);
static void CB2_ReturnFromNaming(void);
static void Task_JuniperIntro(u8 taskId);

static void VBlankCB_JuniperIntro(void)
{
    if (sJI.pendingFrame)
    {
        CpuCopy32(sJI.pendingFrame, (void *)(OBJ_VRAM0 + GetSpriteTileStartByTag(TAG_JUNIPER) * TILE_SIZE_4BPP), FRAME_BYTES);
        sJI.pendingFrame = NULL;
    }
    SetGpuReg(REG_OFFSET_BG3VOFS, sJI.backY);
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void CB2_JuniperIntro(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    RunTextPrinters();
    UpdatePaletteFade();
}

// her idle animation: BW's ticks are 60 a second, like our frames
static void UpdateIdle(void)
{
    if (sJI.juniper == 0xFF)
        return;
    if (sJI.idleTicks)
    {
        sJI.idleTicks--;
        return;
    }
    sJI.idleStep = (sJI.idleStep + 1) % ARRAY_COUNT(sJuniperIdle);
    sJI.idleTicks = sJuniperIdle[sJI.idleStep][1];
    sJI.pendingFrame = sJuniperFrames[sJuniperIdle[sJI.idleStep][0]];
}

static void HideSprite(u8 *id, bool32 trainer)
{
    if (*id == 0xFF)
        return;
    if (trainer)
        FreeAndDestroyTrainerPicSprite(*id);
    else
        FreeAndDestroyMonPicSprite(*id);
    *id = 0xFF;
}

static void SetupScene(void)
{
    struct SpriteSheet sheet = {sJuniperFrames[0], FRAME_BYTES, TAG_JUNIPER};
    struct SpritePalette pal = {sJuniperPal, TAG_JUNIPER};

    SetVBlankCallback(NULL);
    SetHBlankCallback(NULL);
    SetGpuReg(REG_OFFSET_DISPCNT, 0);
    DmaFill16(3, 0, VRAM, VRAM_SIZE);
    DmaFill32(3, 0, OAM, OAM_SIZE);
    DmaFill16(3, 0, PLTT, PLTT_SIZE);
    ResetPaletteFade();
    ScanlineEffect_Stop();
    ResetSpriteData();
    ResetAllPicSprites();
    FreeAllSpritePalettes();
    ResetTasks();
    FreeAllWindowBuffers();
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
    ChangeBgX(BG_TEXT, 0, BG_COORD_SET);
    ChangeBgY(BG_TEXT, 0, BG_COORD_SET);
    ChangeBgX(BG_BACK, 8 << 8, BG_COORD_SET);     // BW's 256 wide screen, its middle 240
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_BLDY, 0);

    LoadBgTiles(BG_BACK, sBgTiles, sizeof(sBgTiles), 0);
    LoadBgTilemap(BG_BACK, sBgMap, sizeof(sBgMap), 0);
    LoadPalette(sBgPal, BG_PLTT_ID(0), PLTT_SIZE_4BPP);

    InitWindows(sWindowTemplates);
    LoadMessageBoxGfx(WIN_TEXT, DIALOG_TILE, BG_PLTT_ID(15));
    LoadPalette(GetOverworldTextboxPalettePtr(), BG_PLTT_ID(15), PLTT_SIZE_4BPP);
    FillWindowPixelBuffer(WIN_TEXT, PIXEL_FILL(1));
    PutWindowTilemap(WIN_TEXT);
    DrawDialogFrameWithCustomTile(WIN_TEXT, FALSE, DIALOG_TILE);
    CopyWindowToVram(WIN_TEXT, COPYWIN_FULL);

    LoadSpriteSheet(&sheet);
    LoadSpritePalette(&pal);
    sJI.juniper = CreateSprite(&sJuniperTemplate, JUNIPER_X, JUNIPER_Y, 0);
    SetSubspriteTables(&gSprites[sJI.juniper], sJuniperSubsprites);
    sJI.idleStep = 0;
    sJI.idleTicks = sJuniperIdle[0][1];
    sJI.pendingFrame = sJuniperFrames[sJuniperIdle[0][0]];
    sJI.minccino = sJI.cheren = sJI.bianca = 0xFF;

    CreateTask(Task_JuniperIntro, 0);
    BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_1D_MAP | DISPCNT_OBJ_ON);
    ShowBg(BG_TEXT);
    ShowBg(BG_BACK);
    EnableInterrupts(INTR_FLAG_VBLANK);
    SetVBlankCallback(VBlankCB_JuniperIntro);
    SetMainCallback2(CB2_JuniperIntro);
}

void StartJuniperIntro(void)
{
    memset(&sJI, 0, sizeof(sJI));
    sJI.backY = BACK_START_Y;
    sJI.state = JI_FADE_IN;
    SetupScene();
    PlayBGM(INTRO_MUSIC);
}

static void DoAction(u8 act)
{
    switch (act)
    {
    case ACT_MINCCINO_IN:
        sJI.minccino = CreateMonPicSprite(SPECIES_MINCCINO, FALSE, 0, TRUE, 184, 88, 14, TAG_NONE);
        if (sJI.minccino != 0xFFFF)
            gSprites[sJI.minccino].oam.priority = 1;
        PlayCry_Normal(SPECIES_MINCCINO, 0);
        break;
    case ACT_MINCCINO_OUT:
        HideSprite(&sJI.minccino, FALSE);
        break;
    case ACT_CHEREN:
        sJI.cheren = CreateTrainerPicSprite(TRAINER_PIC_BW_37, TRUE, 48, 72, 13, GetTrainerPicTag(TRAINER_PIC_BW_37, TRUE));
        break;
    case ACT_BIANCA:
        sJI.bianca = CreateTrainerPicSprite(TRAINER_PIC_BW_38, TRUE, 192, 72, 12, GetTrainerPicTag(TRAINER_PIC_BW_38, TRUE));
        break;
    case ACT_FRIENDS_OUT:
        HideSprite(&sJI.cheren, TRUE);
        HideSprite(&sJI.bianca, TRUE);
        break;
    }
}

static void Say(const u8 *text)
{
    FillWindowPixelBuffer(WIN_TEXT, PIXEL_FILL(1));
    StringExpandPlaceholders(gStringVar4, text);
    AddTextPrinterForMessage(TRUE);
}

static void Task_JuniperIntro(u8 taskId)
{
    UpdateIdle();
    switch (sJI.state)
    {
    case JI_FADE_IN:
        if (!gPaletteFade.active)
            sJI.state = JI_LIGHT_UP;
        break;
    case JI_LIGHT_UP:     // the room lights up: the white comes down over the grey
        if (sJI.backY > 0)
            sJI.backY -= 2;
        if (sJI.backY <= 0)
        {
            sJI.backY = 0;
            sJI.state = JI_SAY;
        }
        break;
    case JI_SAY:
        DoAction(sScript[sJI.step].before);
        Say(sScript[sJI.step].text);
        sJI.state = JI_WAIT_TEXT;
        break;
    case JI_WAIT_TEXT:
        if (!RunTextPrintersAndIsPrinter0Active())
            sJI.state = JI_WAIT_KEY;
        break;
    case JI_WAIT_KEY:
        if (!JOY_NEW(A_BUTTON | B_BUTTON))
            break;
        if (sScript[sJI.step].after == ACT_NAME)
        {
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
            sJI.state = JI_NAMING_FADE;
        }
        else if (sScript[sJI.step].after == ACT_END)
        {
            FadeOutBGM(4);
            BeginNormalPaletteFade(PALETTES_ALL, 3, 0, 16, RGB_WHITE);
            sJI.state = JI_END_FADE;
        }
        else
        {
            sJI.step++;
            sJI.state = JI_SAY;
        }
        break;
    case JI_NAMING_FADE:
        if (gPaletteFade.active)
            break;
        FreeAllWindowBuffers();
        DestroyTask(taskId);
        DoNamingScreen(NAMING_SCREEN_PLAYER, gSaveBlock2Ptr->playerName, gSaveBlock2Ptr->playerGender, 0, 0, CB2_ReturnFromNaming);
        break;
    case JI_END_FADE:
        if (!gPaletteFade.active && BGMusicStopped())
            sJI.state = JI_EXIT;
        break;
    case JI_EXIT:
        FreeAllWindowBuffers();
        ResetSpriteData();
        ResetAllPicSprites();
        FreeAllSpritePalettes();
        DestroyTask(taskId);
        SetMainCallback2(CB2_NewGame);
        break;
    }
}

static void CB2_ReturnFromNaming(void)
{
    sJI.backY = 0;
    sJI.step = STEP_AFTER_NAME;
    sJI.state = JI_SAY;
    SetupScene();
    PlayBGM(INTRO_MUSIC);
}
