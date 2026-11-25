// Pokémon World web/PC build: stand-ins for code that is assembly on the GBA.
// The sound driver (src/m4a_1.s) is in m4a_engine.c, sound_mixer.c and cgb_audio.c.

#ifdef PORTABLE

#include "global.h"
#include "main.h"

// src/decompress_asm.s and src/libgcnmultiboot.s
void FastUnsafeCopy32(void *dest, const void *src, u32 size)
{
    memcpy(dest, src, size);
}

const u32 LZ77UnCompWRAMOptimized[1];
const u32 LZ77UnCompWRAMOptimized_end[1];

// src/libgcnmultiboot.s: linking with a GameCube isn't possible here.
#include "libgcnmultiboot.h"
void GameCubeMultiBoot_Main(struct GcmbStruct *pStruct) {}
void GameCubeMultiBoot_ExecuteProgram(struct GcmbStruct *pStruct) {}
void GameCubeMultiBoot_Init(struct GcmbStruct *pStruct) {}
void GameCubeMultiBoot_HandleSerialInterrupt(struct GcmbStruct *pStruct) {}
void GameCubeMultiBoot_Quit(void) {}

// src/rom_header.s (filled in by gbafix on the GBA)
const u8 RomHeaderGameCode[GAME_CODE_LENGTH] = {'B', 'P', 'E', 'E'};
const u8 RomHeaderSoftwareVersion = 0;

// src/crt0.s: reloads EWRAM's initial values on the GBA; the page reloads instead.
void ReInitializeEWRAM(void) {}

// Set by the GBA linker script.
void CB2_InitCopyrightScreenAfterBootup(void);
void gInitialMainCB2(void)
{
    CB2_InitCopyrightScreenAfterBootup();
}

// src/libisagbprn.c (mGBA's debug log): to the browser console instead.
#include <stdarg.h>
#include <stdio.h>
bool32 (MgbaOpen)(void) { return TRUE; }
void (MgbaClose)(void) {}
void (MgbaPrintf)(s32 level, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    printf("\n");
}
void (MgbaAssert)(const char *file, s32 line, const char *message, bool32 stop)
{
    printf("Assertion failed: %s:%d: %s\n", file, (int)line, message);
}

#endif // PORTABLE
