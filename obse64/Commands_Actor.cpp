#include "GameScript.h"
#include "GameConsole.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "ParamInfos.h"

// classic OBSE's commands about actors: whether one is essential, and its spells
// (Commands_Classic.cpp registers them)

// An actor base's flags: classic TESActorBaseData's, at +0x08 of that component (+0x48 in NPCs
// and creatures). Despite their names, IsRefEssential and SetRefEssential work on the base, as
// vanilla SetEssential does (0x06A18F20), which also marks the flags changed so a save keeps them.
enum
{
	kActorBaseFlag_Essential =	0x02,
	kActorBaseChanged_Flags =	0x10,
};

static u8 * GetActorBaseData(TESObjectREFR * refr)
{
	if(!refr || !refr->baseForm) return nullptr;

	return (u8 *)Runtime_DynamicCast(refr->baseForm, RTTI_TESForm, RTTI_TESActorBaseData);
}

static bool Cmd_IsRefEssential_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(u8 * data = GetActorBaseData(thisObj))
	{
		if(*(u32 *)(data + 0x08) & kActorBaseFlag_Essential)
			*result = 1;
	}

	if(IsConsoleMode())
		Console_Print("IsRefEssential >> %d", (int)*result);

	return true;
}

static bool Cmd_SetRefEssential_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 essential = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &essential)) return true;

	if(u8 * data = GetActorBaseData(thisObj))
	{
		u32 & flags = *(u32 *)(data + 0x08);
		if(essential)
			flags |= kActorBaseFlag_Essential;
		else
			flags &= ~kActorBaseFlag_Essential;

		typedef void (* _MarkChanged)(void * component, u32 changeFlags);
		auto markChanged = reinterpret_cast <_MarkChanged>((*(void ***)data)[0xA0 / 8]);
		markChanged(data, kActorBaseChanged_Flags);
	}

	return true;
}

// An actor's spells, the player's included, are its base's TESSpellList (+0x98 in NPCs and
// creatures), which AddSpell (0x0659F1D0) and RemoveSpell (0x065C5350) change: a list of
// { form, next } nodes from +0x08, and the game's own check for one spell walks it (0x068869A0).
// As classic, the leveled spell lists the base also has don't count.
typedef bool (* _TESSpellList_HasSpell)(void * spellList, TESForm * spell);
static RelocAddr <_TESSpellList_HasSpell> TESSpellList_HasSpell(0x068869A0);

static bool Cmd_HasSpell_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * spell = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &spell)) return true;

	if(spell && thisObj && thisObj->baseForm)
	{
		if(void * spellList = Runtime_DynamicCast(thisObj->baseForm, RTTI_TESForm, RTTI_TESSpellList))
		{
			if(TESSpellList_HasSpell(spellList, spell))
				*result = 1;
		}
	}

	if(IsConsoleMode())
		Console_Print("HasSpell >> %d", (int)*result);

	return true;
}

CommandInfo kCommandInfo_HasSpell =
{
	"HasSpell", "hspl",
	0,
	"returns 1 if the actor has the spell",
	1,
	1, kParams_OneSpellItem,
	Cmd_HasSpell_Execute
};

CommandInfo kCommandInfo_IsRefEssential =
{
	"IsRefEssential", "",
	0,
	"returns 1 if the calling reference is essential",
	1,
	0, nullptr,
	Cmd_IsRefEssential_Execute
};

CommandInfo kCommandInfo_SetRefEssential =
{
	"SetRefEssential", "",
	0,
	"changes the calling reference to essential",
	1,
	1, kParams_OneInt,
	Cmd_SetRefEssential_Execute
};
