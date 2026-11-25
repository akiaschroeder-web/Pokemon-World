#ifndef GUARD_PLATFORM_WEB3D_H
#define GUARD_PLATFORM_WEB3D_H

// Pokémon World (web build): Unova's 3D maps (src/platform/web3d.c).
#ifdef PLATFORM_WEB

struct Sprite;

extern bool8 gWeb3DWorldOam[128];   // OAM slots the 3D view draws (the 2D picture skips them)

void Web3D_ResetCamera(void);
void Web3D_ShiftCamera(s32 dx, s32 dy);
void Web3D_BeginOamBuffer(void);
void Web3D_MarkOam(const struct Sprite *sprite, u32 firstOam, u32 endOam);
void Web3D_LatchOam(void);
void Web3D_EndFrame(void);
bool32 Web3D_IsActive(void);

#endif // PLATFORM_WEB
#endif // GUARD_PLATFORM_WEB3D_H
