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

@ Pokémon World: the Sevii Islands for a player who never played FireRed's Sevii story (e.g. one
@ who started in Johto). FireRed's Bill takes you from Cinnabar to One Island, where you get the
@ Tri-Pass and his Meteorite to deliver (VAR_MAP_SCENE_ONE_ISLAND_POKEMON_CENTER_1F 0 -> 1). After
@ the time skip that scene can't happen, so Heart & Soul's Bill (Goldenrod) starts it here, at
@ state 1; FireRed's Sevii story then goes on as usual (Meteorite, Celio's Ruby and Sapphire,
@ Rainbow Pass). Called at the start of Bill's talk script (import_hns.py pw_script_patches).
EventScript_PwBillSeviiOffer::
	goto_if_unset FLAG_PW_KANTO_TIME_SKIP, EventScript_PwBillSeviiOffer_No
	goto_if_ne VAR_MAP_SCENE_ONE_ISLAND_POKEMON_CENTER_1F, 0, EventScript_PwBillSeviiOffer_No
	msgbox EventScript_PwBillSeviiOffer_Text_Celio, MSGBOX_DEFAULT
	giveitem ITEM_TRI_PASS
	giveitem ITEM_METEORITE
	setflag FLAG_SYS_SEVII_MAP_123
	setvar VAR_MAP_SCENE_ONE_ISLAND_POKEMON_CENTER_1F, 1
	msgbox EventScript_PwBillSeviiOffer_Text_Ferry, MSGBOX_DEFAULT
	closemessage
	release
	end

EventScript_PwBillSeviiOffer_No::
	return

EventScript_PwBillSeviiOffer_Text_Celio:
	.string "BILL: Oh, hey, {PLAYER}!\n"
	.string "Got a minute? I've got a favor to ask.\p"
	.string "My pal CELIO runs the POKéMON CENTER\n"
	.string "on ONE ISLAND, in the SEVII ISLANDS.\p"
	.string "We're linking his PC network up with\n"
	.string "mine, and he needs this METEORITE.\l"
	.string "Could you take it to him for me?\p"
	.string "Here, this TRI-PASS gets you on the\n"
	.string "SEAGALLOP ferries.$"

EventScript_PwBillSeviiOffer_Text_Ferry:
	.string "The SEAGALLOP leaves from VERMILION's\n"
	.string "harbor. I'll meet you on ONE ISLAND!$"
