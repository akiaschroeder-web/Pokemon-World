// Pokémon World web/PC build: stand-ins for code that is assembly on the GBA
// (src/m4a_1.s) until the C sound engine is in.
// Songs and sound effects end as soon as they start, so nothing waits on them.

#ifdef PORTABLE

#include "global.h"
#include "gba/m4a_internal.h"

u32 umul3232H32(u32 a, u32 b)
{
    return (u32)(((u64)a * b) >> 32);
}

void SoundMain(void) {}
void SoundMainBTM(void) {}
void RealClearChain(void *x) {}
void m4aSoundVSync(void) {}

void MPlayJumpTableCopy(MPlayFunc *mplayJumpTable)
{
}

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    track->flags = 0;
}

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    s32 i;
    if (mplayInfo->ident != ID_NUMBER)
        return;
    for (i = 0; i < mplayInfo->trackCount; i++)
        mplayInfo->tracks[i].flags = 0;
    mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
    if (mplayInfo->MPlayMainNext)
        mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);
}

#define PLY_STUB(name) void name(struct MusicPlayerInfo *m, struct MusicPlayerTrack *t) {}
PLY_STUB(ply_fine)
PLY_STUB(ply_goto)
PLY_STUB(ply_patt)
PLY_STUB(ply_pend)
PLY_STUB(ply_rept)
PLY_STUB(ply_prio)
PLY_STUB(ply_tempo)
PLY_STUB(ply_keysh)
PLY_STUB(ply_voice)
PLY_STUB(ply_vol)
PLY_STUB(ply_pan)
PLY_STUB(ply_bend)
PLY_STUB(ply_bendr)
PLY_STUB(ply_lfos)
PLY_STUB(ply_lfodl)
PLY_STUB(ply_mod)
PLY_STUB(ply_modt)
PLY_STUB(ply_tune)
PLY_STUB(ply_port)
PLY_STUB(ply_endtie)

void ply_note(u32 note_cmd, struct MusicPlayerInfo *m, struct MusicPlayerTrack *t) {}

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

#endif // PORTABLE
