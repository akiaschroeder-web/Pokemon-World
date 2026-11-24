@ Pokémon World: Kanto's time skip. Becoming Johto's Champion (Heart & Soul's first Hall of Fame,
@ PokemonLeague_HallOfFame_Hns) ends FireRed's Kanto: from now on there is only the Gold/Silver-era
@ ("3 years later") Kanto, whether or not FireRed's Kanto story was played. Warps into FireRed's
@ Kanto are redirected by src/kanto_time_skip.c; Heart & Soul's later-Kanto set-up runs here.
@ Also run by the debug menu (Flags & Vars: Kanto time skip).
EventScript_PwKantoTimeSkip::
	setflag FLAG_PW_KANTO_TIME_SKIP
	callnative KantoTimeSkip_Native_FixSavedWarps
	call SSAqua_1F_Hns_EventScript_KantoFlagHeap
	return

Debug_EventScript_PwKantoTimeSkip::
	call EventScript_PwKantoTimeSkip
	end
