SandgemTown_PokemonLab_MapScripts::
	map_script MAP_SCRIPT_ON_TRANSITION, SandgemLab_OnTransition
	.byte 0

SandgemLab_OnTransition:
	call LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_CP_SAFE
	call LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_OAK
	checkplayergender
	goto_if_eq VAR_RESULT, MALE, SandgemLab_EventScript_SetDawn
	setvar VAR_OBJ_GFX_ID_0, OBJ_EVENT_GFX_SINNOH_LUCAS
	end

SandgemLab_EventScript_SetDawn::
	setvar VAR_OBJ_GFX_ID_0, OBJ_EVENT_GFX_SINNOH_DAWN
	end

SandgemLab_EventScript_SceneLeft::
	lockall
	applymovement LOCALID_PLAYER, SandgemLab_Movement_WalkToRowanLeft
	waitmovement 0
	goto SandgemLab_EventScript_Scene

SandgemLab_EventScript_SceneRight::
	lockall
	applymovement LOCALID_PLAYER, SandgemLab_Movement_WalkToRowanRight
	waitmovement 0
	goto SandgemLab_EventScript_Scene

SandgemLab_EventScript_Scene::
	call Sinnoh_EventScript_BufferCounterpartName
	bufferleadmonspeciesname STR_VAR_2
	msgbox SandgemLab_Text_RowanWelcome, MSGBOX_DEFAULT
	msgbox SandgemLab_Text_PtNickname, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, SandgemLab_EventScript_PtNoNick
	setvar VAR_0x8004, 0
	call Common_EventScript_NameReceivedPartyMon
SandgemLab_EventScript_PtNoNick:
	msgbox SandgemLab_Text_PtSomething, MSGBOX_DEFAULT
	msgbox SandgemLab_Text_PtRecordData, MSGBOX_YESNO
SandgemLab_EventScript_PtAskDex:
	goto_if_eq VAR_RESULT, YES, SandgemLab_EventScript_PtDexYes
	msgbox SandgemLab_Text_PtAskAgain, MSGBOX_YESNO
	goto SandgemLab_EventScript_PtAskDex
SandgemLab_EventScript_PtDexYes:
	msgbox SandgemLab_Text_PtGoodAnswer, MSGBOX_DEFAULT
	playfanfare MUS_OBTAIN_ITEM
	message SandgemLab_Text_ReceivedPokedex
	waitfanfare
	waitmessage
	setflag FLAG_SYS_POKEDEX_GET
	special SetUnlockedPokedexFlags
	setflag FLAG_RECEIVED_POKEDEX_FROM_BIRCH
	msgbox SandgemLab_Text_RowanPokedex, MSGBOX_DEFAULT
	call Sinnoh_EventScript_BufferCounterpartName
	checkplayergender
	call_if_eq VAR_RESULT, MALE, SandgemLab_Text_PtCounterpart_ScrD
	call_if_eq VAR_RESULT, FEMALE, SandgemLab_Text_PtCounterpart_ScrL
	closemessage
	applymovement LOCALID_SANDGEM_LAB_ASSISTANT, Common_Movement_FacePlayer
	waitmovement 0
	msgbox SandgemLab_Text_AssistantGoHome, MSGBOX_DEFAULT
	closemessage
	setflag FLAG_HIDE_LAB_DAWN
	setvar VAR_SINNOH_STORY, 9
	releaseall
	end

SandgemLab_EventScript_Rowan::
	lock
	faceplayer
	goto_if_set FLAG_SYS_GAME_CLEAR, SandgemLab_EventScript_SNRowanDex
SandgemLab_EventScript_RowanNormal:
	msgbox SandgemLab_Text_RowanAdvice, MSGBOX_DEFAULT
	release
	end

SandgemLab_EventScript_Assistant::
	lock
	call Sinnoh_EventScript_BufferCounterpartName
	faceplayer
	msgbox SandgemLab_Text_AssistantAdvice, MSGBOX_DEFAULT
	release
	end

SandgemLab_Movement_WalkToRowanLeft:
	walk_up
	walk_up
	walk_up
	walk_up
	walk_up
	walk_up
	step_end

SandgemLab_Movement_WalkToRowanRight:
	walk_left
	walk_up
	walk_up
	walk_up
	walk_up
	walk_up
	walk_up
	step_end

SandgemLab_Text_RowanWelcome:
	.string "Professor: Well, well, look who’s here.\p"
	.string "{PLAYER}, was it? Let’s have a look at\n"
	.string "your POKéMON.\p"
	.string "Professor: ...\p"
	.string "Hmm... I see...\p"
	.string "This POKéMON seems to be rather happy.\p"
	.string "Professor: When I first saw you two\n"
	.string "about to step into the tall grass\p"
	.string "without POKéMON, I was shocked.\p"
	.string "I was astounded by these foolhardy\n"
	.string "children.\p"
	.string "But now, you’ve astounded me in an\n"
	.string "entirely different way.\p"
	.string "Already, there is a bond growing\n"
	.string "between you and that POKéMON.\p"
	.string "I feel privileged to have met you.\p"
	.string "I’m sure {STR_VAR_2} feels the same way\n"
	.string "about you.\p"
	.string "That’s why I’ll ask you to cherish that\n"
	.string "{STR_VAR_2} of yours.$"

SandgemLab_Text_ReceivedPokedex:
	.string "{PLAYER} obtained the POKéDEX!$"

SandgemLab_Text_RowanPokedex:
	.string "Rowan: That POKéDEX is a very\n"
	.string "high-tech device.\p"
	.string "It will automatically record data on\n"
	.string "every kind of POKéMON you encounter.\p"
	.string "{PLAYER}, I ask that you go everywhere\n"
	.string "and meet every kind of\p"
	.string "POKéMON in this region!\p"
	.string "Rowan: When you walked up Route 201\n"
	.string "with your POKéMON, what did you feel?\p"
	.string "I’ve lived for 60 long years. Even now,\n"
	.string "I get a thrill when\p"
	.string "I’m with a POKéMON.\p"
	.string "Now, you should know that there are\n"
	.string "countless POKéMON in this world.\p"
	.string "That means there are just as many\n"
	.string "thrills waiting for you out there!\p"
	.string "Now go!\p"
	.string "{PLAYER}, your grand adventure begins\n"
	.string "right now!$"

SandgemLab_Text_AssistantBalls:
	.string "{STR_VAR_1}: Hi! I'm {STR_VAR_1}.\n"
	.string "I'm helping the Professor\n"
	.string "fill his POKéDEX.\p"
	.string "You'll need these to catch\n"
	.string "POKéMON. Here, take them!$"

SandgemLab_Text_AssistantGoHome:
	.string "{STR_VAR_1}: The Professor wants to talk\n"
	.string "to you outside. Come on!$"

SandgemLab_Text_RowanAdvice:
	.string "ROWAN: A POKéMON grows when it is\n"
	.string "trusted. Remember that.$"

SandgemLab_Text_AssistantAdvice:
	.string "{STR_VAR_1}: Weaken a wild POKéMON\n"
	.string "before you throw a POKé BALL.\l"
	.string "It works much better that way!$"

SandgemLab_Movement_DawnLeaves:
	walk_down
	walk_down
	walk_down
	walk_down
	walk_down
	walk_down
	walk_down
	walk_left
	step_end

LittlerootTown_ProfessorBirchsLab_EventScript_PtNpc1::
	msgbox LittlerootTown_ProfessorBirchsLab_Text_PtNpc1, MSGBOX_NPC
	end

LittlerootTown_ProfessorBirchsLab_Text_PtNpc1:
	.string "The Professor studies POKéMON\n"
	.string "evolution. He says 90 percent of\l"
	.string "POKéMON are related through it!$"

LittlerootTown_ProfessorBirchsLab_EventScript_PtNpc2::
	msgbox LittlerootTown_ProfessorBirchsLab_Text_PtNpc2, MSGBOX_NPC
	end

LittlerootTown_ProfessorBirchsLab_Text_PtNpc2:
	.string "We record data on every POKéMON we\n"
	.string "can find. Your POKéDEX helps a\l"
	.string "lot!$"

LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_OAK:
	setflag FLAG_SN_HIDE_LAB_OAK
	goto LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_OAK_End
	clearflag FLAG_SN_HIDE_LAB_OAK
LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_OAK_End:
	return

SandgemLab_EventScript_SNRowanDex::
	setvar VAR_0x8004, 0
	special ScriptGetPokedexInfo
	goto_if_eq VAR_RESULT, TRUE, SandgemLab_EventScript_SNRowanDex_HasNational
	buffernumberstring STR_VAR_1, VAR_0x8005
	msgbox SandgemLab_EventScript_SNRowanDex_T1, MSGBOX_DEFAULT
	msgbox SandgemLab_EventScript_SNRowanDex_T2, MSGBOX_DEFAULT
	goto_if_ge VAR_0x8005, 210, SandgemLab_EventScript_SNRowanDex_Oak
	msgbox SandgemLab_EventScript_SNRowanDex_T3, MSGBOX_DEFAULT
	release
	end
SandgemLab_EventScript_SNRowanDex_HasNational:
	call SandgemLab_EventScript_SNGiveRadar
	msgbox SandgemLab_EventScript_SNRowanDex_Many, MSGBOX_DEFAULT
	release
	end
SandgemLab_EventScript_SNRowanDex_Oak:
	msgbox SandgemLab_EventScript_SNRowanDex_Met, MSGBOX_DEFAULT
	closemessage
	clearflag FLAG_SN_HIDE_LAB_OAK
	addobject LOCALID_SN_LAB_OAK
	applymovement LOCALID_SN_LAB_OAK, SandgemLab_EventScript_SNRowanDex_OakIn
	waitmovement 0
	msgbox SandgemLab_EventScript_SNRowanDex_O1, MSGBOX_DEFAULT
	msgbox SandgemLab_EventScript_SNRowanDex_O2, MSGBOX_DEFAULT
	msgbox SandgemLab_EventScript_SNRowanDex_O3, MSGBOX_DEFAULT
	applymovement OBJ_EVENT_ID_PLAYER, Common_Movement_WalkInPlaceFasterDown
	waitmovement 0
	msgbox SandgemLab_EventScript_SNRowanDex_O4, MSGBOX_DEFAULT
	special EnableNationalPokedex
	playfanfare MUS_OBTAIN_ITEM
	message SandgemLab_EventScript_SNRowanDex_O5
	waitfanfare
	waitmessage
	msgbox SandgemLab_EventScript_SNRowanDex_O6, MSGBOX_DEFAULT
	msgbox SandgemLab_EventScript_SNRowanDex_O7, MSGBOX_DEFAULT
	msgbox SandgemLab_EventScript_SNRowanDex_Birds, MSGBOX_DEFAULT
	setvar VAR_0x8005, 60
	setvar VAR_0x8004, SPECIES_ARTICUNO
	special SinnohAddRoamer
	setvar VAR_0x8004, SPECIES_ZAPDOS
	special SinnohAddRoamer
	setvar VAR_0x8004, SPECIES_MOLTRES
	special SinnohAddRoamer
	closemessage
	applymovement LOCALID_SN_LAB_OAK, SandgemLab_EventScript_SNRowanDex_OakOut
	waitmovement 0
	playse SE_EXIT
	removeobject LOCALID_SN_LAB_OAK
	setflag FLAG_SN_HIDE_LAB_OAK
	waitse
	call SandgemLab_EventScript_SNGiveRadar
	msgbox SandgemLab_EventScript_SNRowanDex_Off, MSGBOX_DEFAULT
	msgbox SandgemLab_EventScript_SNRowanDex_Many, MSGBOX_DEFAULT
	release
	end

SandgemLab_EventScript_SNOakTalk::
	msgbox SandgemLab_EventScript_SNRowanDex_O6, MSGBOX_NPC
	end

SandgemLab_EventScript_SNRowanDex_OakIn:
	walk_up
	walk_up
	walk_up
	walk_up
	walk_up
	walk_up
	step_end

SandgemLab_EventScript_SNRowanDex_OakOut:
	walk_fast_down
	walk_fast_down
	walk_fast_down
	walk_fast_down
	walk_fast_down
	walk_fast_down
	step_end

SandgemLab_EventScript_SNRowanDex_T1:
	.string "Rowan: Ah, {PLAYER}!\p"
	.string "You’ve come to show me your progress\n"
	.string "on the POKéDEX?$"

SandgemLab_EventScript_SNRowanDex_T2:
	.string "Hmm...\p"
	.string "So, you’ve seen {STR_VAR_1} POKéMON.$"

SandgemLab_EventScript_SNRowanDex_T3:
	.string "Excellent work, {PLAYER}!\p"
	.string "The POKéMON of the Sinnoh region are\n"
	.string "being added to your POKéDEX.\p"
	.string "This will certainly help with my studies\n"
	.string "of POKéMON evolution...$"

SandgemLab_EventScript_SNRowanDex_Met:
	.string "Rowan: Hm! {PLAYER}, I’ve been waiting\n"
	.string "to see you!\p"
	.string "You’ve met every kind of POKéMON there\n"
	.string "is in the Sinnoh region!\p"
	.string "This will help immensely with my studies\n"
	.string "on POKéMON evolution...$"

SandgemLab_EventScript_SNRowanDex_O1:
	.string "Oak: Greetings, Professor Rowan! It’s\n"
	.string "been a very long time!\p"
	.string "I’ll tell you, Sinnoh certainly is a long\n"
	.string "trip from Kanto.\p"
	.string "Of course, if it means meeting new\n"
	.string "POKéMON, there’s no distance too\p"
	.string "great for the likes of us to travel.$"

SandgemLab_EventScript_SNRowanDex_O2:
	.string "Rowan: Oh! If it isn’t my old colleague,\n"
	.string "Professor Oak!\p"
	.string "I should have expected as much from\n"
	.string "the world’s authority on POKéMON!\p"
	.string "We always used to joke, “Where there\n"
	.string "are POKéMON, you’ll find Oak!”\p"
	.string "It’s good to see that hasn’t changed\n"
	.string "one bit!$"

SandgemLab_EventScript_SNRowanDex_O3:
	.string "Professor Oak, let me introduce you to\n"
	.string "my young assistant.\p"
	.string "This youngster has filled every page\n"
	.string "of the Sinnoh POKéDEX for me.$"

SandgemLab_EventScript_SNRowanDex_O4:
	.string "Oak: Ah, well, very glad to meet you! As\n"
	.string "you’ve heard, my name is Oak.\p"
	.string "I’ve been hearing a great deal about\n"
	.string "you from Professor Rowan lately.\p"
	.string "He’s been exuberant in praise about a\n"
	.string "fantastic young Trainer.\p"
	.string "I see that you live up to--no--that\n"
	.string "you’ve surpassed his praise.\p"
	.string "You’ve also got an impeccable sense of\n"
	.string "timing.\p"
	.string "You see, I had an errand to run for\n"
	.string "Professor Rowan on my visit here.\p"
	.string "He’d asked me to bring the data for the\n"
	.string "National POKéDEX, you see.\p"
	.string "So, since you’re here, let me upgrade\n"
	.string "your POKéDEX with the National Mode.\p"
	.string "After all, there are many kinds of\n"
	.string "POKéMON in this world of ours!$"

SandgemLab_EventScript_SNRowanDex_O5:
	.string "{PLAYER}’s POKéDEX was upgraded with\n"
	.string "the National Mode!$"

SandgemLab_EventScript_SNRowanDex_O6:
	.string "Oak: I’m afraid it won’t be easy to\n"
	.string "complete the National POKéDEX.\p"
	.string "However, I’m sure you will make an\n"
	.string "honest attempt on our behalf!$"

SandgemLab_EventScript_SNRowanDex_O7:
	.string "Rowan: Have no fear, {PLAYER} will get\n"
	.string "the job done.\p"
	.string "By the way, Professor Oak, what\n"
	.string "compelled you to visit this region?$"

SandgemLab_EventScript_SNRowanDex_Birds:
	.string "Oak: Why did I come? Well, I have been\n"
	.string "chasing the legendary bird POKéMON\l"
	.string "from Kanto: ARTICUNO, ZAPDOS and\l"
	.string "MOLTRES.\p"
	.string "It seems they have flown all the way to\n"
	.string "Sinnoh! They roam the routes of the\l"
	.string "region now.\p"
	.string "{PLAYER}, if you meet them, try to catch\n"
	.string "them! Oops! I’ll be late for my meeting!$"

SandgemLab_EventScript_SNRowanDex_Off:
	.string "Rowan: Off he goes, as busy as ever...$"

SandgemLab_EventScript_SNRowanDex_Many:
	.string "Rowan: The world is immense. There are\n"
	.string "many more POKéMON.\p"
	.string "That means there are many more thrills\n"
	.string "for you to seek and enjoy!$"

LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_CP_SAFE:
	setflag FLAG_SN_HIDE_LAB_CP_SAFE
	call SandgemLab_EventScript_SNCPSafe
	goto_if_lt VAR_SINNOH_STORY, 33, LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_CP_SAFE_End
	goto_if_set FLAG_SN_LAB_CP_SAFE, LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_CP_SAFE_End
	clearflag FLAG_SN_HIDE_LAB_CP_SAFE
LittlerootTown_ProfessorBirchsLab_EventScript_SNVis_LAB_CP_SAFE_End:
	return

SandgemLab_EventScript_SNCPSafe::
	checkplayergender
	goto_if_eq VAR_RESULT, FEMALE, SandgemLab_EventScript_SNCPSafe_L
	setvar VAR_OBJ_GFX_ID_1, OBJ_EVENT_GFX_SINNOH_DAWN
	return
SandgemLab_EventScript_SNCPSafe_L:
	setvar VAR_OBJ_GFX_ID_1, OBJ_EVENT_GFX_SINNOH_LUCAS
	return

SandgemLab_EventScript_SNCPSafe_Talk::
	lock
	faceplayer
	checkplayergender
	goto_if_eq VAR_RESULT, FEMALE, SandgemLab_EventScript_SNCPSafe_M_L
	msgbox SandgemLab_EventScript_SNCPSafe_M_D, MSGBOX_DEFAULT
	goto SandgemLab_EventScript_SNCPSafe_M_N
SandgemLab_EventScript_SNCPSafe_M_L:
	msgbox SandgemLab_EventScript_SNCPSafe_M_Lu, MSGBOX_DEFAULT
SandgemLab_EventScript_SNCPSafe_M_N:
	closemessage
	setflag FLAG_SN_LAB_CP_SAFE
	release
	end

SandgemLab_EventScript_SNCPSafe_M_D:
	.string "Dawn: The professor did a lot of\n"
	.string "checking up after you left.\p"
	.string "He was really worried about you,\n"
	.string "BARRY...\p"
	.string "He contacted acquaintances looking\n"
	.string "for help, and...\p"
	.string "BARRY, I’m glad to see you safe!\n"
	.string "Welcome back!$"

SandgemLab_EventScript_SNCPSafe_M_Lu:
	.string "Lucas: After you left, the professor\n"
	.string "did a lot of checking around.\p"
	.string "He got really worried about you, BARRY.\p"
	.string "He got in touch with people he knows to\n"
	.string "rustle up help...\p"
	.string "You didn’t need it, BARRY. I’m glad to\n"
	.string "see you back safe, too.$"

SandgemLab_Text_PtRecordData:
	.string "Rowan: Allow me to properly introduce\n"
	.string "myself first.\p"
	.string "My name, as you know, is Rowan. I study\n"
	.string "POKéMON.\p"
	.string "First of all, I want to know exactly\n"
	.string "what kinds of POKéMON live in the\p"
	.string "Sinnoh region.\p"
	.string "To do so, it is necessary to collect\n"
	.string "data using the POKéDEX.\p"
	.string "This is what I wish to ask of you.\p"
	.string "I want to entrust you with this\n"
	.string "POKéDEX.\p"
	.string "Will you use it to record data on all\n"
	.string "the POKéMON in Sinnoh for me?$"

SandgemLab_Text_PtAskAgain:
	.string "Rowan: ...\p"
	.string "...\p"
	.string "...\p"
	.string "...\p"
	.string "I can stand here without speaking for\n"
	.string "hours. I’m a very patient man.\p"
	.string "Let me ask you again, {PLAYER}.\p"
	.string "I want to entrust you with this\n"
	.string "POKéDEX.\p"
	.string "Will you use it to record data on all\n"
	.string "the POKéMON in Sinnoh for me?$"

SandgemLab_Text_PtGoodAnswer:
	.string "Rowan: Hm!\p"
	.string "Good answer!$"

SandgemLab_Text_PtNickname:
	.string "Professor: Well, would you like to give\n"
	.string "it a nickname?$"

SandgemLab_Text_PtSomething:
	.string "Professor: Er-hem! Let’s move on to\n"
	.string "the main topic.\p"
	.string "There is something I want you to do for\n"
	.string "me.$"

SandgemLab_Text_PtCounterpart:
	.string "{STR_VAR_1}: I’m so glad that you’re\n"
	.string "kind toward POKéMON!\p"
	.string "If you weren’t, I’d have to... Oh, I just\n"
	.string "can’t say it...\p"
	.string "{STR_VAR_1}: I’ve got one, too!\p"
	.string "{STR_VAR_1}: The POKéMON that I met\n"
	.string "for the first time was {STR_VAR_2}!\p"
	.string "If you would have chosen {STR_VAR_2} on\n"
	.string "Route 201, we’d have the same\p"
	.string "POKéMON now!\p"
	.string "Not that it matters, but...\p"
	.string "Anyway, I’m Dawn.\p"
	.string "I also help the professor add pages to\n"
	.string "the POKéDEX.\p"
	.string "So, in a sense, I’m just like you.\p"
	.string "I just got a little head start on you,\n"
	.string "that’s all.\p"
	.string "I’ll be happy to teach you things. Glad\n"
	.string "to meet you, {PLAYER}!$"

SandgemLab_Text_PtCounterpart_ScrD:
	bufferspeciesname STR_VAR_2, SPECIES_PIPLUP
	goto_if_eq VAR_STARTER_MON, 0, SandgemLab_Text_PtCounterpart_ScrD2
	bufferspeciesname STR_VAR_2, SPECIES_TURTWIG
	goto_if_eq VAR_STARTER_MON, 1, SandgemLab_Text_PtCounterpart_ScrD2
	bufferspeciesname STR_VAR_2, SPECIES_CHIMCHAR
SandgemLab_Text_PtCounterpart_ScrD2:
	checkplayergender
	call_if_eq VAR_RESULT, FEMALE, SandgemLab_Text_PtCounterpart_SNLucasCall
	call_if_ne VAR_RESULT, FEMALE, SandgemLab_Text_PtCounterpart_SNDawnCall
	return
SandgemLab_Text_PtCounterpart_ScrL:
	msgbox SandgemLab_Text_PtCounterpart_Lu, MSGBOX_DEFAULT
	return

SandgemLab_Text_PtCounterpart_Lu:
	.string "{STR_VAR_1}: Boy, am I glad you’re kind\n"
	.string "toward POKéMON.\p"
	.string "If you weren’t, well... I don’t even want\n"
	.string "to think about it.\p"
	.string "{STR_VAR_1}: I’ve got the same kind of\n"
	.string "POKéDEX as you.\p"
	.string "{STR_VAR_1}: The professor asked me to\n"
	.string "help, so I’m filling the POKéDEX pages,\l"
	.string "too.\p"
	.string "That makes us comrades working toward\n"
	.string "the same goal.\p"
	.string "I’ll teach you stuff later, OK?$"

SandgemLab_Text_PtCounterpart_SNDawnCall:
	msgbox SandgemLab_Text_PtCounterpart, MSGBOX_DEFAULT
	return

SandgemLab_Text_PtCounterpart_SNLucasCall:
	msgbox SandgemLab_Text_PtCounterpart_SNLu, MSGBOX_DEFAULT
	return

SandgemLab_Text_PtCounterpart_SNLu:
	.string "{STR_VAR_1}: Boy, am I glad you’re kind\n"
	.string "toward POKéMON.\p"
	.string "If you weren’t, well... I don’t even want\n"
	.string "to think about it.\p"
	.string "{STR_VAR_1}: I’ve got the same kind of\n"
	.string "POKéDEX as you.\p"
	.string "{STR_VAR_1}: The professor asked me to\n"
	.string "help, so I’m filling the POKéDEX pages,\l"
	.string "too.\p"
	.string "That makes us comrades working toward\n"
	.string "the same goal.\p"
	.string "I’ll teach you stuff later, OK?$"

SandgemLab_EventScript_SNGiveRadar:
	checkitem ITEM_POKE_RADAR
	goto_if_eq VAR_RESULT, TRUE, SandgemLab_EventScript_SNGiveRadarEnd
	msgbox SandgemLab_Text_SNRadar1, MSGBOX_DEFAULT
	giveitem ITEM_POKE_RADAR
	msgbox SandgemLab_Text_SNRadar2, MSGBOX_DEFAULT
SandgemLab_EventScript_SNGiveRadarEnd:
	return

SandgemLab_Text_SNRadar1:
	.string "Rowan: Oh, yes. Take this with you as\n"
	.string "well.$"

SandgemLab_Text_SNRadar2:
	.string "Rowan: That’s the POKéMON Radar, or\n"
	.string "just Poké Radar for short.\p"
	.string "Use it, and it will indicate grass\n"
	.string "patches where POKéMON are lurking.\p"
	.string "I’d prepared that to help my field\n"
	.string "assistants put together the Sinnoh\l"
	.string "POKéDEX, but you took care of that!\p"
	.string "I’m sure it will be useful for your goal\n"
	.string "of filling the National POKéDEX.$"
