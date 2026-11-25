@ Pokémon World: Hearthome's Contest Hall lobby. The Sinnoh project used Lilycove's lobby for it;
@ it is a copy of that map now, with Hearthome's people and exits, running Lilycove's lobby scripts.
HearthomeCity_ContestLobby_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, HearthomeCity_ContestLobby_OnTransition
	map_script MAP_SCRIPT_ON_FRAME_TABLE, LilycoveCity_ContestLobby_OnFrame
	.byte 0

HearthomeCity_ContestLobby_OnTransition:
	call HearthomeCity_ContestLobby_EventScript_SNVis_FANTINA
	call LilycoveCity_ContestLobby_EventScript_TryShowContestReporter
	call LilycoveCity_ContestLobby_EventScript_TryShowBlendMaster
	setvar VAR_PW_CONTEST_LOBBY, 1
	end

HearthomeCity_ContestLobby_EventScript_SNVis_FANTINA:
	setflag FLAG_SN_HIDE_FANTINA
	goto_if_set FLAG_SN_MET_FANTINA, HearthomeCity_ContestLobby_EventScript_SNVis_FANTINA_End
	goto_if_set FLAG_SINNOH_BADGE03_GET, HearthomeCity_ContestLobby_EventScript_SNVis_FANTINA_End
	clearflag FLAG_SN_HIDE_FANTINA
HearthomeCity_ContestLobby_EventScript_SNVis_FANTINA_End:
	return

HearthomeCity_ContestLobby_EventScript_SNFantina::
	lock
	faceplayer
	msgbox HearthomeCity_ContestLobby_EventScript_SNFantina_T, MSGBOX_DEFAULT
	closemessage
	setflag FLAG_SN_MET_FANTINA
	sn_walkto LOCALID_SN_FANTINA, 14, 11, SN_SPEED_NORMAL, 0, 0, 0
	waitmovement LOCALID_SN_FANTINA
	removeobject LOCALID_SN_FANTINA
	setflag FLAG_SN_HIDE_FANTINA
	release
	end

HearthomeCity_ContestLobby_EventScript_SNFantina_T:
	.string "Yes? I beg your pardon?\p"
	.string "Ah, oui, Contests you are here for? I\n"
	.string "hope very much you enjoy them.\p"
	.string "Incidentally, Fantina, the Gym Leader\n"
	.string "of Hearthome, is also a Contestant.\p"
	.string "She is, how do you say, très\n"
	.string "fantastique!\p"
	.string "Fantina being myself, of course!\p"
	.string "Mmm, let me see... You also seem to be\n"
	.string "très strong.\p"
	.string "Enfin, so it shall be. I wait for you at\n"
	.string "the Gym!$"
