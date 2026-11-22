// Pokémon World web/PC build: the save chip, as 128 KB of memory.
// Same interface as src/agb_flash*.c (which talk to the real chip on a cartridge).
// The platform layer stores this memory to disk or to the browser after writes.

#ifdef PORTABLE

#include "global.h"
#include "gba/flash_internal.h"
#include "agb_flash.h"

#define FLASH_SECTOR_SIZE  0x1000
#define FLASH_SECTOR_COUNT 32

extern unsigned char gPlatformFlash[];
void Web_MarkSaveDirty(void);

u8 gFlashTimeoutFlag;
u16 gFlashNumRemainingBytes;

static u8 *SectorPtr(u16 sectorNum)
{
    return &gPlatformFlash[(sectorNum % FLASH_SECTOR_COUNT) * FLASH_SECTOR_SIZE];
}

static u16 PlatformProgramFlashByte(u16 sectorNum, u32 offset, u8 data)
{
    if (offset >= FLASH_SECTOR_SIZE)
        return 0x8000;
    SectorPtr(sectorNum)[offset] = data;
    Web_MarkSaveDirty();
    return 0;
}

static u16 PlatformProgramFlashSector(u16 sectorNum, u8 *src)
{
    memcpy(SectorPtr(sectorNum), src, FLASH_SECTOR_SIZE);
    Web_MarkSaveDirty();
    return 0;
}

static u16 PlatformEraseFlashChip(void)
{
    memset(gPlatformFlash, 0xFF, FLASH_SECTOR_SIZE * FLASH_SECTOR_COUNT);
    Web_MarkSaveDirty();
    return 0;
}

static u16 PlatformEraseFlashSector(u16 sectorNum)
{
    memset(SectorPtr(sectorNum), 0xFF, FLASH_SECTOR_SIZE);
    Web_MarkSaveDirty();
    return 0;
}

static u16 PlatformWaitForFlashWrite(u8 phase, u8 *addr, u8 lastData)
{
    return 0;
}

u16 (*ProgramFlashByte)(u16, u32, u8) = PlatformProgramFlashByte;
u16 (*ProgramFlashSector)(u16, u8 *) = PlatformProgramFlashSector;
u16 (*EraseFlashChip)(void) = PlatformEraseFlashChip;
u16 (*EraseFlashSector)(u16) = PlatformEraseFlashSector;
u16 (*WaitForFlashWrite)(u8, u8 *, u8) = PlatformWaitForFlashWrite;

u16 IdentifyFlash(void)
{
    return 0; // found
}

u16 SetFlashTimerIntr(u8 timerNum, void (**intrFunc)(void))
{
    return 0;
}

void ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    memcpy(dest, SectorPtr(sectorNum) + offset, size);
}

u32 ProgramFlashSectorAndVerify(u16 sectorNum, u8 *src)
{
    ProgramFlashSector(sectorNum, src);
    return 0;
}

#endif // PORTABLE
