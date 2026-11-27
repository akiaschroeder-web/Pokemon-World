#ifndef GUARD_BW_SCRIPT_H
#define GUARD_BW_SCRIPT_H

// Pokémon World: Pokémon Black's own event scripts, run in Unova (src/bw_script.c).
struct ScriptContext;

void Script_BwScript(struct ScriptContext *ctx);   // bwscript <zone>, <script id>
void Script_BwScriptInit(struct ScriptContext *ctx);
void BwScript_OnEnterMap(void);
extern bool8 gBwCameraTransition;  // walking over a map connection (the objects stay), not a warp
extern bool8 gBwIntroDemoPending;   // a new Unova game: fly over Nuvema Town first
u16 BwSeasonMusic(u16 song);                       // a Unova song -> this season's version                    // before a map's transition scripts
bool32 BwFlagGet(u16 flag);
u32 BwMapReplaceBits(void);                        // MapReplace ids that are on (bit n = id n)

#endif // GUARD_BW_SCRIPT_H
