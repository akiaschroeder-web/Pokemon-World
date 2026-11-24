#ifndef GUARD_KANTO_TIME_SKIP_H
#define GUARD_KANTO_TIME_SKIP_H

// Pokémon World: Kanto's time skip. Once Johto's story ends (FLAG_PW_KANTO_TIME_SKIP), there is
// only the Gold/Silver-era Kanto (Heart & Soul's *_Hns maps): any warp into FireRed's mainland
// Kanto is sent to the matching map instead. FireRed's Sevii Islands stay as they are.

struct ScriptContext;

bool32 KantoTimeSkip_IsActive(void);
// Changes a warp into FireRed's Kanto to the matching Gold/Silver-era place (if the time skip
// happened). Returns TRUE if it changed it.
bool32 KantoTimeSkip_RedirectWarp(struct WarpData *warp);
// The saved warps (last heal location, escape rope, continue game...) after the switch.
void KantoTimeSkip_FixSavedWarps(void);
// callnative: right after FLAG_PW_KANTO_TIME_SKIP is set
void KantoTimeSkip_Native_FixSavedWarps(struct ScriptContext *ctx);

#endif // GUARD_KANTO_TIME_SKIP_H
