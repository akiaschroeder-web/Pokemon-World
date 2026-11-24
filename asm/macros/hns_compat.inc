@ Pokémon World: script commands that Heart & Soul added to its event script engine,
@ so its Johto and later-Kanto map scripts assemble. Each one is a native call with the
@ same arguments, ported in src/hns_specials.c (HnsNative_*).

	@ Heart & Soul's applymovement with a 2-byte local id: ours already takes one.
	.macro applymovement2 localId:req, movements:req, map
	.ifb \map
	applymovement \localId, \movements
	.else
	applymovement \localId, \movements, \map
	.endif
	.endm

	@ Gives one of Heart & Soul's named gift Pokémon (1 = Kenya, 2 = Shuckie, 3 = Eevee).
	.macro givenamedmon number:req
	callnative HnsNative_GiveNamedMon
	.2byte \number
	.endm

	@ Takes a named gift Pokémon back (1 = Kenya, 2 = Shuckie).
	.macro removenamedmon number:req
	callnative HnsNative_RemoveNamedMon
	.2byte \number
	.endm

	@ Takes the Pokémon in party slot VAR_0x8004 if it is the given species.
	.macro removegenericmon number:req
	callnative HnsNative_RemoveGenericMon
	.2byte \number
	.endm

	.macro remove5mons
	callnative HnsNative_Remove5Mons
	.endm

	@ Checks the Pokémon in party slot VAR_0x8004 against one of Baoba's lists.
	.macro baobacheckmon number:req
	callnative HnsNative_BaobaCheckMon
	.2byte \number
	.endm

	@ Gives the Odd Egg (1-7).
	.macro giveoddegg number:req
	callnative HnsNative_GiveOddEgg
	.2byte \number
	.endm

	@ Gives Battle Points.
	.macro givebp number:req
	callnative HnsNative_GiveBP
	.2byte \number
	.endm

	.macro buffermoncategory stringVarId:req, species:req
	callnative HnsNative_BufferMonCategory
	stringvar \stringVarId
	.2byte \species
	.endm

	@ Is the Pokémon in party slot VAR_0x8004 at least this level?
	.macro checkpartymonlevel level:req
	callnative HnsNative_CheckPartyMonLevel
	.2byte \level
	.endm

	@ Like setwildbattle, but the Pokémon is shiny (Lake of Rage's red Gyarados).
	.macro setwildbattleshiny species:req, level:req, item=ITEM_NONE
	callnative HnsNative_SetWildBattleShiny
	.2byte \species
	.byte \level
	.2byte \item
	.endm

	@ Heart & Soul: lets the player pick an item from a bag pocket (VAR_ITEM_ID; ITEM_NONE if
	@ cancelled). Its maps only ever ask for a Berry, so this is expansion's Berry chooser.
	.macro chooseitem pocket:req
	.if \pocket != BERRIES_POCKET
	.error "chooseitem: only BERRIES_POCKET is supported"
	.endif
	fadescreen FADE_TO_BLACK
	special Bag_ChooseBerry
	.endm

	@ Heart & Soul's checkpartymove for moves that aren't expansion field moves (Headbutt):
	@ VAR_RESULT = the first party slot whose Pokémon knows the move, or failing that can learn
	@ it from a tutor/TM (Heart & Soul's rule), else PARTY_SIZE; VAR_0x8004 = its species.
	.macro hnscheckpartymove move:req
	callnative HnsNative_CheckPartyMove
	.2byte \move
	.endm
