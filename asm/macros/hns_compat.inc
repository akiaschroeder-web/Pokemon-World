@ Pokémon World: script commands that Heart & Soul added to its event script engine,
@ so its Johto and later-Kanto map scripts assemble. Each one is a native call with the
@ same arguments; the natives in src/hns_stubs.c are stubs for now (TODO, later stages).

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

	@ TODO: Heart & Soul makes this one shiny.
	.macro setwildbattleshiny species:req, level:req, item=ITEM_NONE
	setwildbattle \species, \level, \item
	.endm

	@ Heart & Soul: lets the player pick an item from a bag pocket (VAR_ITEM_ID).
	.macro chooseitem pocket:req
	callnative HnsNative_ChooseItem
	.2byte \pocket
	.endm
