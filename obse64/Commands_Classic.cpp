#include "Commands_Classic.h"
#include "CommandTable.h"
#include "FunctionScripts.h"
#include "GameConsole.h"
#include "GameExtraData.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <map>
#include <set>
#include <string>
#include <Windows.h>

// Classic OBSE commands past release 6, registered at their classic opcodes (from
// ClassicCommandTable.inl) so scripts compiled against classic OBSE call the right
// command. Commands that haven't been ported get a stub that reports itself and fails
// the script line, the same way the unknown opcode failed before.

CommandInfo * GetCommandInfo(u32 opcode);	// Hooks_Script.cpp

// a reference passed as the argument stands for its base object, as in classic OBSE
static TESForm * FormArgOrBase(TESForm * form, TESObjectREFR * thisObj)
{
	if(form)
	{
		if(auto * refr = DYNAMIC_CAST(form, TESForm, TESObjectREFR))
			return refr->baseForm;

		return form;
	}

	return thisObj ? thisObj->baseForm : nullptr;
}

static bool Cmd_GetBaseObject_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj || !thisObj->baseForm) return true;

	SetFormResult(result, thisObj->baseForm->refID);

	if(IsConsoleMode())
		Console_Print("GetBaseObject >> %08X", thisObj->baseForm->refID);

	return true;
}

// The interface manager keeps the reference the player would activate at +0x158. Unreal casts
// the player's line of sight and hands the hit over (0x0655A460); the setter (0x065D6950) stores
// it there while it's within reach and clears it otherwise, and the player's own activation reads
// it from there (0x065694B0). Classic read HUDInfoMenu::crosshairRef, which stays empty here.
static RelocPtr <u8 *> g_interfaceManager(0x0941F808);

static bool Cmd_GetCrosshairRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESObjectREFR * refr = nullptr;
	if(u8 * intfc = *g_interfaceManager)
	{
		if(auto * form = *(TESForm **)(intfc + 0x158))
			refr = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
	}

	if(refr)
		SetFormResult(result, refr->refID);

	if(IsConsoleMode())
		Console_Print("GetCrosshairRef >> %08X (%s)", refr ? refr->refID : 0, refr ? GetFullName(refr) : "nothing");

	return true;
}

static bool Cmd_GetObjectType_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
		*result = form->typeID;

	if(IsConsoleMode())
		Console_Print("GetObjectType >> %d", (int)*result);

	return true;
}

static bool IsFormType_Execute(COMMAND_ARGS, u8 typeID, const char * name)
{
	*result = 0;

	TESForm * form = nullptr;
	ExtractArgs(EXTRACT_ARGS, &form);

	form = FormArgOrBase(form, thisObj);
	if(form && form->typeID == typeID)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("%s >> %d", name, (int)*result);

	return true;
}

static CommandInfo kCommandInfo_GetBaseObject =
{
	"GetBaseObject", "",
	0,
	"returns the base object of the reference",
	1,
	0, nullptr,
	Cmd_GetBaseObject_Execute
};

static CommandInfo kCommandInfo_GetCrosshairRef =
{
	"GetCrosshairRef", "",
	0,
	"returns the reference under the crosshair",
	0,
	0, nullptr,
	Cmd_GetCrosshairRef_Execute
};

// The game's controls (classic OSInputGlobals, class Controls here) are at +0x30 of the OS
// globals. Unreal sets each control's state in it, and QueryControlState compares (1 << query)
// with that: query 0 held (state 1), 1 just pressed (state 2), 2 just released (state 4).
// Classic's DirectInput path is still in it, for when Unreal isn't the source.
static RelocPtr <u8 *> g_osGlobals(0x0941F740);

typedef u32 (* _Controls_QueryControlState)(void * controls, u32 control, u32 query);
static RelocAddr <_Controls_QueryControlState> Controls_QueryControlState(0x0673EA30);

enum
{
	kControl_Count =	29,	// the mappable controls, Forward (0) to Grab (0x1C)

	kKeyQuery_Held =	0,
	kKeyQuery_Pressed =	1,
};

bool IsControlDown(u32 control)
{
	if(control >= kControl_Count) return false;

	u8 * osGlobals = *g_osGlobals;
	void * controls = osGlobals ? *(void **)(osGlobals + 0x30) : nullptr;
	if(!controls) return false;

	return Controls_QueryControlState(controls, control, kKeyQuery_Held) ||
		Controls_QueryControlState(controls, control, kKeyQuery_Pressed);
}

static bool Cmd_IsControlPressed_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(IsControlDown(control))
		*result = 1;

	if(IsConsoleMode())
		Console_Print("IsControlPressed %d >> %d", control, (int)*result);

	return true;
}

// 1 the first time each script asks after the control went down, then 0 until it's released
// and pressed again, as in classic
static bool Cmd_OnControlDown_Execute(COMMAND_ARGS)
{
	static std::map <u32, std::set <u32>> s_reported;	// script form ID -> controls it has been told are down

	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control) || !script) return true;

	// Script derives from TESForm
	auto & reported = s_reported[reinterpret_cast <TESForm *>(script)->refID];

	if(IsControlDown(control))
	{
		if(reported.insert(control).second)
			*result = 1;
	}
	else
		reported.erase(control);

	return true;
}

// Ownership is kept in a reference's extra data, or in its cell's for references (and their
// contents) that have none of their own. Vanilla SetOwnership and ClearOwnership change only the
// owner, so a required rank stays with the reference.
static TESForm * GetOwner(const BaseExtraList & extraList)
{
	BaseExtraList::Locker lock;

	auto * xOwner = extraList.Get <ExtraOwnership>();
	return xOwner ? xOwner->owner : nullptr;
}

static u32 GetOwningFactionRequiredRank(const BaseExtraList & extraList)
{
	BaseExtraList::Locker lock;

	auto * xRank = extraList.Get <ExtraRank>();
	return xRank ? xRank->rank : 0;
}

static void SetOwnerResult(double * result, TESForm * owner, const char * name)
{
	if(owner)
		SetFormResult(result, owner->refID);

	if(IsConsoleMode())
		Console_Print("%s >> %08X (%s)", name, owner ? owner->refID : 0, owner ? GetFullName(owner) : "none");
}

static bool Cmd_GetOwner_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj)
		SetOwnerResult(result, GetOwner(thisObj->extraList), "GetOwner");

	return true;
}

static bool Cmd_GetParentCellOwner_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj && thisObj->parentCell)
		SetOwnerResult(result, GetOwner(thisObj->parentCell->extraData), "GetParentCellOwner");

	return true;
}

static bool Cmd_GetOwningFactionRequiredRank_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj)
		*result = GetOwningFactionRequiredRank(thisObj->extraList);

	if(IsConsoleMode())
		Console_Print("GetOwningFactionRequiredRank >> %d", (int)*result);

	return true;
}

static bool Cmd_GetParentCellOwningFactionRequiredRank_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj && thisObj->parentCell)
		*result = GetOwningFactionRequiredRank(thisObj->parentCell->extraData);

	if(IsConsoleMode())
		Console_Print("GetParentCellOwningFactionRequiredRank >> %d", (int)*result);

	return true;
}

// The game's check of whether taking or using a reference is a crime for the player (classic
// TESObjectREFR::IsOffLimitsToPlayer): ownership, the player's faction ranks, rented-room globals
// and doors' own rules. The crosshair uses it to say "Steal" (0x06765C00).
typedef bool (* _IsOffLimitsToPlayer)(TESObjectREFR * refr);
static RelocAddr <_IsOffLimitsToPlayer> IsOffLimitsToPlayer(0x06582560);

static bool Cmd_IsOffLimits_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * actor = nullptr;
	if(!thisObj || !ExtractArgs(EXTRACT_ARGS, &actor)) return true;

	PlayerCharacter * player = PlayerCharacter::Get();
	if(actor && (!player || actor != player->baseForm))
	{
		// classic works out another actor's crimes itself, from its factions: not ported yet, so
		// fail the line, as an unported command does, rather than answer wrongly
		static bool s_reported = false;
		if(!s_reported)
		{
			s_reported = true;
			_MESSAGE("IsOffLimits for an actor other than the player is not implemented yet");
		}

		return false;
	}

	if(IsOffLimitsToPlayer(thisObj))
		*result = 1;

	if(IsConsoleMode())
		Console_Print("IsOffLimits >> %d", (int)*result);

	return true;
}

// The game's value of an item, what a theft's bounty is worked out from (0x06955040): the value
// component's for most items, and for potions and ingredients their magic item's, which works out
// an autocalc potion's value. -1 for a form with no value.
typedef s32 (* _GetItemValue)(TESForm * form);
static RelocAddr <_GetItemValue> GetItemValue(0x067D6A20);

static bool Cmd_GetGoldValue_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
	{
		s32 value = GetItemValue(form);
		if(value > 0)
			*result = value;
	}

	if(IsConsoleMode())
		Console_Print("GetGoldValue >> %d", (int)*result);

	return true;
}

// A quest item is a base object with form flag 0x400 (TESForm::IsQuestItem). SetQuestItem also
// marks the flags changed, so a save keeps the change, as vanilla SetQuestObject (0x06A1AEA0) does.
static bool Cmd_IsQuestItem_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form && form->IsQuestItem())
		*result = 1;

	if(IsConsoleMode())
		Console_Print("IsQuestItem >> %d", (int)*result);

	return true;
}

static bool Cmd_SetQuestItem_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 questItem = 0;
	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &questItem, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
		form->SetQuestItem(questItem != 0);

	return true;
}

// A model's path is the BSString at +0x08 of TESModel. Armor and clothing have no TESModel: their
// models are in the biped model component, where the game loads the male ground (world) model,
// MOD2, into +0x70 (0x06820F40). Paths are set with the game's string setter, which is all
// TESModel's own setter does (0x067D6740).
typedef bool (* _BSString_Set)(BSStringT * str, const char * src, u32 minLength);
static RelocAddr <_BSString_Set> BSString_Set(0x06562E40);

static BSStringT * ModelPathOf(TESForm * form, bool maleGround)
{
	if(!form) return nullptr;

	if(maleGround)
	{
		auto * biped = DYNAMIC_CAST(form, TESForm, TESBipedModelForm);
		return biped ? &biped->groundModel[0].nifPath : nullptr;
	}

	auto * model = DYNAMIC_CAST(form, TESForm, TESModel);
	return model ? &model->nifPath : nullptr;
}

// classic's ModelPathIncludes and friends: is text part of the path, ignoring case
static bool ComparePath_Execute(COMMAND_ARGS, bool maleGround, const char * name)
{
	*result = 0;

	char text[BUFSIZ] = { 0 };
	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &text, &form)) return true;

	BSStringT * path = ModelPathOf(FormArgOrBase(form, thisObj), maleGround);
	if(path && path->m_data && *text)
	{
		std::string haystack(path->m_data), needle(text);
		for(char & c : haystack) c = char(tolower(u8(c)));
		for(char & c : needle) c = char(tolower(u8(c)));

		if(haystack.find(needle) != std::string::npos)
			*result = 1;
	}

	if(IsConsoleMode())
		Console_Print("%s >> %d", name, (int)*result);

	return true;
}

static bool CopyPath_Execute(COMMAND_ARGS, bool maleGround)
{
	*result = 0;

	TESForm * src = nullptr;
	TESForm * dest = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &src, &dest)) return true;

	BSStringT * srcPath = ModelPathOf(FormArgOrBase(src, nullptr), maleGround);
	BSStringT * destPath = ModelPathOf(FormArgOrBase(dest, thisObj), maleGround);
	if(srcPath && destPath && srcPath != destPath)
		BSString_Set(destPath, srcPath->m_data, 0);

	return true;
}

static bool Cmd_CompareModelPath_Execute(COMMAND_ARGS)
{
	return ComparePath_Execute(PASS_COMMAND_ARGS, false, "CompareModelPath");
}

static bool Cmd_CompareMaleGroundPath_Execute(COMMAND_ARGS)
{
	return ComparePath_Execute(PASS_COMMAND_ARGS, true, "CompareMaleGroundPath");
}

static bool Cmd_CopyModelPath_Execute(COMMAND_ARGS)
{
	return CopyPath_Execute(PASS_COMMAND_ARGS, false);
}

static bool Cmd_CopyMaleGroundPath_Execute(COMMAND_ARGS)
{
	return CopyPath_Execute(PASS_COMMAND_ARGS, true);
}

static ParamInfo kParams_OneOptionalNPC[1] =
{
	{	"npc", kParamType_NPC, 1 },
};

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

static CommandInfo kCommandInfo_IsRefEssential =
{
	"IsRefEssential", "",
	0,
	"returns 1 if the calling reference is essential",
	1,
	0, nullptr,
	Cmd_IsRefEssential_Execute
};

static CommandInfo kCommandInfo_SetRefEssential =
{
	"SetRefEssential", "",
	0,
	"changes the calling reference to essential",
	1,
	1, kParams_OneInt,
	Cmd_SetRefEssential_Execute
};

// in Commands_Inventory.cpp, next to the equipped item lookup they use
extern CommandInfo kCommandInfo_GetObjectCharge;
extern CommandInfo kCommandInfo_GetEquippedCurrentCharge;
extern CommandInfo kCommandInfo_ModEquippedCurrentCharge;

// in Commands_String.cpp, with the strings they keep
extern CommandInfo kCommandInfo_sv_Construct;
extern CommandInfo kCommandInfo_sv_Set;
extern CommandInfo kCommandInfo_sv_Length;
extern CommandInfo kCommandInfo_sv_SubString;

// in Commands_Events.cpp, with the handlers they keep
extern CommandInfo kCommandInfo_SetEventHandler;
extern CommandInfo kCommandInfo_RemoveEventHandler;

static CommandInfo kCommandInfo_IsOffLimits =
{
	"IsOffLimits", "",
	0,
	"returns true if activating the calling reference would result in a crime for the actor",
	1,
	1, kParams_OneOptionalNPC,
	Cmd_IsOffLimits_Execute
};

static CommandInfo kCommandInfo_GetGoldValue =
{
	"GetGoldValue", "",
	0,
	"returns the gold value of the object or calling reference",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetGoldValue_Execute
};

static CommandInfo kCommandInfo_GetOwner =
{
	"GetOwner", "",
	0,
	"returns the owner of the calling reference",
	1,
	0, nullptr,
	Cmd_GetOwner_Execute
};

static CommandInfo kCommandInfo_GetParentCellOwner =
{
	"GetParentCellOwner", "",
	0,
	"returns the owner of the calling reference's cell",
	1,
	0, nullptr,
	Cmd_GetParentCellOwner_Execute
};

static CommandInfo kCommandInfo_GetOwningFactionRequiredRank =
{
	"GetOwningFactionRequiredRank", "",
	0,
	"returns the required rank for ownership of the calling reference",
	1,
	0, nullptr,
	Cmd_GetOwningFactionRequiredRank_Execute
};

static CommandInfo kCommandInfo_GetParentCellOwningFactionRequiredRank =
{
	"GetParentCellOwningFactionRequiredRank", "",
	0,
	"returns the required rank for ownership of the calling reference's cell",
	1,
	0, nullptr,
	Cmd_GetParentCellOwningFactionRequiredRank_Execute
};

static CommandInfo kCommandInfo_IsControlPressed =
{
	"IsControlPressed", "",
	0,
	"returns true if the key or button assigned to control is pressed",
	0,
	1, kParams_OneInt,
	Cmd_IsControlPressed_Execute
};

static CommandInfo kCommandInfo_OnControlDown =
{
	"OnControlDown", "",
	0,
	"returns true each time the key or button assigned to control is depressed",
	0,
	1, kParams_OneInt,
	Cmd_OnControlDown_Execute
};

static CommandInfo kCommandInfo_GetObjectType =
{
	"GetObjectType", "",
	0,
	"returns the type id of the specified object or the reference on which it was called",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetObjectType_Execute
};

#define IS_FORM_TYPE(name, type, what)													\
	static bool Cmd_##name##_Execute(COMMAND_ARGS)										\
	{																					\
		return IsFormType_Execute(PASS_COMMAND_ARGS, kFormType_##type, #name);			\
	}																					\
																						\
	static CommandInfo kCommandInfo_##name =											\
	{																					\
		#name, "",																		\
		0,																				\
		"returns 1 if the specified base object is " what " and 0 if it is not",		\
		0,																				\
		1, kParams_OneOptionalInventoryObject,											\
		Cmd_##name##_Execute															\
	};

IS_FORM_TYPE(IsWeapon,		Weapon,			"a weapon")
IS_FORM_TYPE(IsAmmo,		Ammo,			"ammo")
IS_FORM_TYPE(IsArmor,		Armor,			"armor")
IS_FORM_TYPE(IsClothing,	Clothing,		"clothing")
IS_FORM_TYPE(IsBook,		Book,			"a book")
IS_FORM_TYPE(IsIngredient,	Ingredient,		"an ingredient")
IS_FORM_TYPE(IsContainer,	Container,		"a container")
IS_FORM_TYPE(IsKey,			Key,			"a key")
IS_FORM_TYPE(IsAlchemyItem,	AlchemyItem,	"an alchemy item")
IS_FORM_TYPE(IsApparatus,	Apparatus,		"an apparatus")
IS_FORM_TYPE(IsSoulGem,		SoulGem,		"a soul gem")
IS_FORM_TYPE(IsSigilStone,	SigilStone,		"a sigil stone")
IS_FORM_TYPE(IsDoor,		Door,			"a door")
IS_FORM_TYPE(IsActivator,	Activator,		"an activator")
IS_FORM_TYPE(IsLight,		Light,			"a light")
IS_FORM_TYPE(IsMiscItem,	Misc,			"a misc item")
IS_FORM_TYPE(IsFurniture,	Furniture,		"furniture")
IS_FORM_TYPE(IsFlora,		Flora,			"flora")

#undef IS_FORM_TYPE

static ParamInfo kParams_OneOptionalActorBase[1] =
{
	{	"actor base", kParamType_ActorBase, 1 },
};

static bool Cmd_IsCreature_Execute(COMMAND_ARGS)
{
	return IsFormType_Execute(PASS_COMMAND_ARGS, kFormType_Creature, "IsCreature");
}

static CommandInfo kCommandInfo_IsCreature =
{
	"IsCreature", "",
	0,
	"returns 1 if the passed actor base is a creature",
	0,
	1, kParams_OneOptionalActorBase,
	Cmd_IsCreature_Execute
};

// An actor keeps the horse it rides, and a horse its rider, at +0x288 (classic's horseOrRider):
// Character's virtual function 0xF0 and Creature's 0xF2 return it, and IsRidingHorse asks them.
enum
{
	kActorOffset_HorseOrRider =	0x288,
};

static bool Cmd_GetRider_Execute(COMMAND_ARGS)
{
	*result = 0;

	u8 * horse = thisObj ? (u8 *)Runtime_DynamicCast(thisObj, RTTI_TESObjectREFR, RTTI_Creature) : nullptr;
	auto * rider = horse ? *(TESObjectREFR **)(horse + kActorOffset_HorseOrRider) : nullptr;
	if(rider)
		SetFormResult(result, rider->refID);

	if(IsConsoleMode())
		Console_Print("GetRider >> %08X", rider ? rider->refID : 0);

	return true;
}

static CommandInfo kCommandInfo_GetRider =
{
	"GetRider", "",
	0,
	"returns a reference to the actor currently riding the calling horse",
	1,
	0, nullptr,
	Cmd_GetRider_Execute
};

enum
{
	kMenuType_Magic =	1022,

	// classic's tile values for a menu list's items: what kind of item, and its index
	kTileValue_user7 =	0xFB5,
	kTileValue_user13 =	0xFBB,
};

// The open menus' tiles, by menu type (classic's g_TileMenuArray); a tile menu's Menu is at +0x88,
// and Menu's destructor (0x066F84F0) clears both.
typedef u8 * (* _GetTileMenuByType)(u32 menuType);
static RelocAddr <_GetTileMenuByType> GetTileMenuByType(0x066F1F80);

static u8 * GetMenuByType(u32 menuType)
{
	u8 * tileMenu = GetTileMenuByType(menuType);
	return tileMenu ? *(u8 **)(tileMenu + 0x88) : nullptr;
}

// The magic menu is still classic's MagicMenu under Unreal's. Unreal's handlers for hovering or
// clicking an item (0x0656F470, 0x0656F3B0) call 0x06774440 with the menu and the item's index,
// which finds the item's tile and shows its details. Nothing keeps the index, so OBSE64 notes it
// on the way, with the spell it showed, and forgets it when a new magic menu is built (in the
// factory, 0x065DD280).
//
// The menu's list is a copy made when it opens, rebuilt when an item is clicked or the filter
// changes (0x06775DA0, which sends Unreal the new list); RemoveSpell doesn't touch it. A click
// rebuilds the list before the click handler reports the hover, so the hovered index is read in
// the new list; but after a rebuild with no hover since, the hovered row can hold another spell,
// so the selection is only given while the list still has the same node, with the same spell, at
// the hovered index.
typedef void (* _MagicMenu_Hover)(u8 * menu, s32 index);
static RelocAddr <_MagicMenu_Hover> MagicMenu_Hover(0x06774440);
static RelocAddr <uintptr_t> kMagicMenuHover_HoverCall(0x0656F4C1);
static RelocAddr <uintptr_t> kMagicMenuHover_ClickCall(0x0656F448);

typedef u8 * (* _MagicMenu_Construct)(u8 * menu);
static RelocAddr <_MagicMenu_Construct> MagicMenu_Construct(0x067731D0);
static RelocAddr <uintptr_t> kMagicMenu_ConstructCall(0x065DD337);

typedef u8 * (* _MagicMenu_GetItemTile)(u8 * menu, s32 index);
static RelocAddr <_MagicMenu_GetItemTile> MagicMenu_GetItemTile(0x06774A40);

typedef float (* _Tile_GetFloat)(u8 * tile, u32 valueID);
static RelocAddr <_Tile_GetFloat> Tile_GetFloat(0x066F1EE0);

struct MagicMenuListNode
{
	TESForm				* data;
	MagicMenuListNode	* next;
};

static u8 * s_hoveredMagicMenu = nullptr;
static s32 s_hoveredMagicIndex = 0;
static MagicMenuListNode * s_hoveredMagicNode = nullptr;
static u32 s_hoveredMagicSpellID = 0;

// An item of the magic menu, read as its details are (0x06775570): an item of kind 8 is a scroll
// and of kind 0x10 an active effect; any other is a spell, the index-th, counting from 1, in the
// menu's list at +0x70.
static MagicMenuListNode * GetMagicMenuSpellNode(u8 * menu, s32 index)
{
	u8 * tile = MagicMenu_GetItemTile(menu, index);
	if(!tile) return nullptr;

	s32 kind = s32(Tile_GetFloat(tile, kTileValue_user7));
	if(kind == 8 || kind == 0x10)
	{
		// classic gave a scroll's book for a scroll; not ported yet
		return nullptr;
	}

	s32 position = 1;
	for(auto * node = (MagicMenuListNode *)(menu + 0x70); node; node = node->next, position++)
	{
		if(position == index)
			return node;
	}

	return nullptr;
}

static void MagicMenu_Hover_Hook(u8 * menu, s32 index)
{
	MagicMenu_Hover(menu, index);

	MagicMenuListNode * node = GetMagicMenuSpellNode(menu, index);
	SpellItem * spell = (node && node->data) ? DYNAMIC_CAST(node->data, TESForm, SpellItem) : nullptr;

	s_hoveredMagicMenu = menu;
	s_hoveredMagicIndex = index;
	s_hoveredMagicNode = spell ? node : nullptr;
	s_hoveredMagicSpellID = spell ? node->data->refID : 0;
}

static u8 * MagicMenu_Construct_Hook(u8 * menu)
{
	s_hoveredMagicMenu = nullptr;

	return MagicMenu_Construct(menu);
}

// The hovered spell's form ID, while the list still holds it where it was hovered. The ID kept
// from the hover is given, so a spell freed since isn't read.
static u32 GetMagicMenuSelection()
{
	u8 * menu = GetMenuByType(kMenuType_Magic);
	if(!menu || menu != s_hoveredMagicMenu || !menu[0xAC] || !s_hoveredMagicNode) return 0;

	if(GetMagicMenuSpellNode(menu, s_hoveredMagicIndex) != s_hoveredMagicNode) return 0;

	return s_hoveredMagicSpellID;
}

static bool Cmd_GetActiveMenuSelection_Execute(COMMAND_ARGS)
{
	*result = 0;

	// classic's optional menu type; without it, the menu with the mouse or keyboard focus
	u32 menuType = 0xFFFFFFFF;
	if(!ExtractArgs(EXTRACT_ARGS, &menuType)) return true;

	u32 selection = 0;
	if(menuType == 0xFFFFFFFF || menuType == kMenuType_Magic)
	{
		selection = GetMagicMenuSelection();
	}
	else
	{
		static bool s_reported = false;
		if(!s_reported)
		{
			s_reported = true;
			_MESSAGE("GetActiveMenuSelection only knows the magic menu so far (asked for menu %d)", menuType);
		}
	}

	if(selection)
		SetFormResult(result, selection);

	if(IsConsoleMode())
		Console_Print("GetActiveMenuSelection >> %08X (%s)", selection, GetFullName(selection ? LookupFormByID(selection) : nullptr));

	return true;
}

static void InstallMagicMenuHooks()
{
	uintptr_t hover = MagicMenu_Hover.getUIntPtr();
	uintptr_t construct = MagicMenu_Construct.getUIntPtr();

	if(!IsCallTo(kMagicMenuHover_HoverCall.getUIntPtr(), hover) ||
		!IsCallTo(kMagicMenuHover_ClickCall.getUIntPtr(), hover) ||
		!IsCallTo(kMagicMenu_ConstructCall.getUIntPtr(), construct))
	{
		_ERROR("the magic menu's calls aren't where expected; GetActiveMenuSelection won't see the magic menu");
		return;
	}

	g_branchTrampoline.write5Call(kMagicMenuHover_HoverCall.getUIntPtr(), uintptr_t(MagicMenu_Hover_Hook));
	g_branchTrampoline.write5Call(kMagicMenuHover_ClickCall.getUIntPtr(), uintptr_t(MagicMenu_Hover_Hook));
	g_branchTrampoline.write5Call(kMagicMenu_ConstructCall.getUIntPtr(), uintptr_t(MagicMenu_Construct_Hook));
}

// GetGameLoaded is 1 the first time each script asks after a save is loaded or a new game starts,
// and, as in classic, the first time after the game itself starts. The game's loader (0x0670FA80)
// opens the save with 0x06715190 and gives up if that fails. A new game closes the main menu
// (0x065DE980) from both of its starts, after the confirmation (0x065DEA90) or without one
// (0x065DEC40), once it has found the character generation quest.
typedef void * (* _SaveLoad_OpenSave)(void * saveLoad, void * unk, const char * name, s32 unk2);
static RelocAddr <_SaveLoad_OpenSave> SaveLoad_OpenSave(0x06715190);
static RelocAddr <uintptr_t> kLoadGame_OpenSaveCall(0x0670FB9C);

typedef void (* _MainMenu_Close)();
static RelocAddr <_MainMenu_Close> MainMenu_Close(0x065DE980);
static RelocAddr <uintptr_t> kNewGame_CloseMainMenuCall(0x065DEB5F);
static RelocAddr <uintptr_t> kNewGameUnconfirmed_CloseMainMenuCall(0x065DEDA7);

static bool s_gameLoaded = false;
static u32 s_gameLoadCount = 0;	// for anything that must not outlive the game it was made in

static void OnGameLoaded()
{
	s_gameLoaded = true;
	s_gameLoadCount++;
}

static void * SaveLoad_OpenSave_Hook(void * saveLoad, void * unk, const char * name, s32 unk2)
{
	void * save = SaveLoad_OpenSave(saveLoad, unk, name, unk2);
	if(save)
		OnGameLoaded();

	return save;
}

static void MainMenu_Close_NewGame_Hook()
{
	OnGameLoaded();

	MainMenu_Close();
}

static void InstallGameLoadedHooks()
{
	uintptr_t openSave = SaveLoad_OpenSave.getUIntPtr();
	uintptr_t closeMainMenu = MainMenu_Close.getUIntPtr();

	if(!IsCallTo(kLoadGame_OpenSaveCall.getUIntPtr(), openSave) ||
		!IsCallTo(kNewGame_CloseMainMenuCall.getUIntPtr(), closeMainMenu) ||
		!IsCallTo(kNewGameUnconfirmed_CloseMainMenuCall.getUIntPtr(), closeMainMenu))
	{
		_ERROR("the game's load and new game calls aren't where expected; GetGameLoaded will only report the game starting");
		return;
	}

	g_branchTrampoline.write5Call(kLoadGame_OpenSaveCall.getUIntPtr(), uintptr_t(SaveLoad_OpenSave_Hook));
	g_branchTrampoline.write5Call(kNewGame_CloseMainMenuCall.getUIntPtr(), uintptr_t(MainMenu_Close_NewGame_Hook));
	g_branchTrampoline.write5Call(kNewGameUnconfirmed_CloseMainMenuCall.getUIntPtr(), uintptr_t(MainMenu_Close_NewGame_Hook));
}

static bool Cmd_GetGameLoaded_Execute(COMMAND_ARGS)
{
	static std::set <u32> s_informed;	// script form IDs told since the last load

	*result = 0;

	if(s_gameLoaded)
	{
		s_informed.clear();
		s_gameLoaded = false;
	}

	// Script derives from TESForm
	if(script && s_informed.insert(reinterpret_cast <TESForm *>(script)->refID).second)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("GetGameLoaded >> %d", (int)*result);

	return true;
}

// 1 the first time each script asks after the game starts, and never after a load
static bool Cmd_GetGameRestarted_Execute(COMMAND_ARGS)
{
	static std::set <u32> s_informed;	// script form IDs told since the game started

	*result = 0;

	// Script derives from TESForm
	if(script && s_informed.insert(reinterpret_cast <TESForm *>(script)->refID).second)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("GetGameRestarted >> %d", (int)*result);

	return true;
}

// AddItem tells the player what was added with the game's message that has an icon (classic's
// QueueUIMessage_2, 0x0656C3E0), called from 0x067DD888. AddItemNS runs the game's AddItem with
// that message skipped, as classic did by making the message functions return at once.
typedef bool (* _QueueUIMessageWithIcon)(void * message, float duration, const char * icon, void * sound, u8 unk);
static RelocAddr <_QueueUIMessageWithIcon> QueueUIMessageWithIcon(0x0656C3E0);
static RelocAddr <uintptr_t> kAddItem_MessageCall(0x067DD888);

// EquipItem and UnequipItem tell the player what was equipped with the game's plain message, a
// notification (classic's QueueUIMessage, 0x0656C6B0), called from 0x06A09E6D and 0x06A20D6F,
// which EquipItemNS and UnequipItemNS skip the same way.
struct UIMessage;	// a message for Unreal to show, with MessageEX below

typedef bool (* _QueueUIMessage)(UIMessage * message, void * unk, u32 notification, float duration);
static RelocAddr <_QueueUIMessage> QueueUIMessage(0x0656C6B0);
static RelocAddr <uintptr_t> kEquipItem_MessageCall(0x06A09E6D);
static RelocAddr <uintptr_t> kUnequipItem_MessageCall(0x06A20D6F);

enum
{
	kOpcode_AddItem =		0x1002,
	kOpcode_EquipItem =		0x10EE,
	kOpcode_UnequipItem =	0x10EF,
};

static u32 s_itemMessagesOff = 0;

static bool AddItem_Message_Hook(void * message, float duration, const char * icon, void * sound, u8 unk)
{
	if(s_itemMessagesOff) return false;

	return QueueUIMessageWithIcon(message, duration, icon, sound, unk);
}

static bool EquipItem_Message_Hook(UIMessage * message, void * unk, u32 notification, float duration)
{
	if(s_itemMessagesOff) return false;

	return QueueUIMessage(message, unk, notification, duration);
}

static void InstallItemMessageHook()
{
	if(IsCallTo(kAddItem_MessageCall.getUIntPtr(), QueueUIMessageWithIcon.getUIntPtr()))
		g_branchTrampoline.write5Call(kAddItem_MessageCall.getUIntPtr(), uintptr_t(AddItem_Message_Hook));
	else
		_ERROR("AddItem's message call isn't where expected; AddItemNS will show the message");

	for(uintptr_t call : { kEquipItem_MessageCall.getUIntPtr(), kUnequipItem_MessageCall.getUIntPtr() })
	{
		if(IsCallTo(call, QueueUIMessage.getUIntPtr()))
			g_branchTrampoline.write5Call(call, uintptr_t(EquipItem_Message_Hook));
		else
			_ERROR("an equip message call isn't where expected; EquipItemNS and UnequipItemNS will show it");
	}
}

// runs the game's command with its item messages skipped
static bool RunWithoutItemMessages(u32 opcode, COMMAND_ARGS)
{
	CommandInfo * cmd = GetCommandInfo(opcode);
	if(!cmd || !cmd->execute) return false;

	s_itemMessagesOff++;
	bool ok = cmd->execute(cmd->params, scriptData, thisObj, containingObj, script, locals, result, opcodeOffsetPtr);
	s_itemMessagesOff--;

	return ok;
}

static bool Cmd_AddItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_AddItem, PASS_COMMAND_ARGS);
}

static bool Cmd_EquipItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_EquipItem, PASS_COMMAND_ARGS);
}

static bool Cmd_UnequipItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_UnequipItem, PASS_COMMAND_ARGS);
}

static ParamInfo kParams_AddItem[2] =
{
	{	"item", kParamType_InventoryObject, 0 },
	{	"quantity", kParamType_Integer, 0 },
};

static CommandInfo kCommandInfo_AddItemNS =
{
	"AddItemNS", "",
	0,
	"version of AddItem which doesn't generate UI messages",
	1,
	2, kParams_AddItem,
	Cmd_AddItemNS_Execute
};

static ParamInfo kParams_EquipItem[2] =
{
	{	"item", kParamType_InventoryObject, 0 },
	{	"lockEquip", kParamType_Integer, 1 },
};

static CommandInfo kCommandInfo_EquipItemNS =
{
	"EquipItemNS", "",
	0,
	"version of EquipItem which doesn't generate UI messages",
	1,
	2, kParams_EquipItem,
	Cmd_EquipItemNS_Execute
};

static CommandInfo kCommandInfo_UnequipItemNS =
{
	"UnequipItemNS", "",
	0,
	"version of UnequipItem which doesn't generate UI messages",
	1,
	2, kParams_EquipItem,
	Cmd_UnequipItemNS_Execute
};

// GetFirstRef and GetNextRef walk the references in the player's cell, and in an exterior the
// cells around it to a given depth, one matching reference per call, as classic did. A cell keeps
// its references in the list at +0xB8. An exterior cell (flag 1 of +0x79 clear) has its grid
// coordinates at +0xA0, and its worldspace finds a neighbour by them (0x06720A00, which only looks
// in the worldspace's map of cells, so a cell the game hasn't made yet is skipped, not made).
typedef TESObjectCELL * (* _TESWorldSpace_LookupCell)(TESWorldSpace * world, s32 x, s32 y);
static RelocAddr <_TESWorldSpace_LookupCell> TESWorldSpace_LookupCell(0x06720A00);

enum
{
	// classic's form types for GetFirstRef beyond the game's own
	kRefScanType_Any =			0,
	kRefScanType_Actor =		69,
	kRefScanType_Item =			70,
	kRefScanType_Projectile =	71,
	kRefScanType_MapMarker =	72,
	kRefScanType_Deleted =		90,

	kCellFlag0_Interior =	0x01,

	kRefFlag_Unk02 =	0x02,	// set, along with deleted, on an item picked up from the world
	kRefFlag_Deleted =	0x20,

	kFormID_PlayerBase =	0x07,
	kFormID_MapMarker =		0x10,
};

struct CellRefScan
{
	using Node = BSSimpleList <TESObjectREFR *>::Node;

	TESWorldSpace	* world;		// set for an exterior scan of the cells around the player's
	s32				centerX;
	s32				centerY;
	s32				depth;
	s32				dx;				// the cell being walked, from the centre
	s32				dy;
	TESObjectCELL	* cell;			// the cell being walked, none when the scan is done
	Node			* next;			// the next node of its list to look at
	u32				formType;
	bool			includeTaken;
	bool			includeDeleted;
	u32				gameLoadCount;	// a scan started before a load would walk cells that are gone
};

// one scan per script, as GetNextRef takes no arguments
static std::map <u32, CellRefScan> s_cellRefScans;

static bool RefMatchesScan(const CellRefScan & scan, TESObjectREFR * refr)
{
	TESForm * base = refr->baseForm;
	if(!base) return false;

	bool deleted = (refr->flags & kRefFlag_Deleted) != 0;
	bool taken = (refr->flags & (kRefFlag_Deleted | kRefFlag_Unk02)) == (kRefFlag_Deleted | kRefFlag_Unk02);

	switch(scan.formType)
	{
		case kRefScanType_Any:
			return scan.includeTaken || !taken;

		case kRefScanType_Actor:
			if(deleted && !scan.includeDeleted) return false;

			return (base->typeID == kFormType_Creature) ||
				(base->typeID == kFormType_NPC && base->refID != kFormID_PlayerBase);

		case kRefScanType_Item:
			if(deleted && !scan.includeDeleted) return false;
			if(taken && !scan.includeTaken) return false;

			switch(base->typeID)
			{
				case kFormType_Apparatus:
				case kFormType_Armor:
				case kFormType_Book:
				case kFormType_Clothing:
				case kFormType_Ingredient:
				case kFormType_Misc:
				case kFormType_Weapon:
				case kFormType_Ammo:
				case kFormType_SoulGem:
				case kFormType_Key:
				case kFormType_AlchemyItem:
				case kFormType_SigilStone:
					return true;

				case kFormType_Light:
				{
					auto * light = DYNAMIC_CAST(base, TESForm, TESObjectLIGH);
					return light && light->IsCarriable();
				}
			}

			return false;

		case kRefScanType_Projectile:
			// classic only finds projectiles for the commands that name their shooter
			return false;

		case kRefScanType_MapMarker:
			return (!deleted || scan.includeDeleted) && base->refID == kFormID_MapMarker;

		case kRefScanType_Deleted:
			return deleted;

		default:
			if(deleted && !scan.includeDeleted) return false;
			if(taken && !scan.includeTaken) return false;

			return base->typeID == scan.formType && base->refID != kFormID_PlayerBase;
	}
}

static void WalkCell(CellRefScan & scan, TESObjectCELL * cell)
{
	scan.cell = cell;
	scan.next = &cell->objectList.node;
}

// on to the next cell of the square around the centre that the game has made, row by row
static void NextScanCell(CellRefScan & scan)
{
	scan.cell = nullptr;
	scan.next = nullptr;

	if(!scan.world) return;

	for(;;)
	{
		if(++scan.dx > scan.depth)
		{
			scan.dx = -scan.depth;
			if(++scan.dy > scan.depth) return;
		}

		if(TESObjectCELL * cell = TESWorldSpace_LookupCell(scan.world, scan.centerX + scan.dx, scan.centerY + scan.dy))
		{
			WalkCell(scan, cell);
			return;
		}
	}
}

static void StartCellRefScan(CellRefScan & scan, TESObjectCELL * cell, u32 formType, u32 depth, bool includeTaken, bool includeDeleted)
{
	scan = CellRefScan();
	scan.formType = formType;
	scan.includeTaken = includeTaken;
	scan.includeDeleted = includeDeleted;
	scan.gameLoadCount = s_gameLoadCount;

	if(!(cell->flags0 & kCellFlag0_Interior) && cell->worldSpace && cell->coords && depth)
	{
		scan.world = cell->worldSpace;
		scan.centerX = cell->coords->x;
		scan.centerY = cell->coords->y;
		scan.depth = s32(depth > 0xFF ? 0xFF : depth);	// classic kept the depth in a byte
		scan.dx = -scan.depth - 1;
		scan.dy = -scan.depth;

		NextScanCell(scan);
	}
	else
		WalkCell(scan, cell);
}

// The next matching reference, none once the scan is done. Like classic, the scan keeps its place
// in a cell's list between calls, which suits a loop that runs within one frame.
static TESObjectREFR * ContinueCellRefScan(CellRefScan & scan)
{
	while(scan.cell)
	{
		while(scan.next)
		{
			CellRefScan::Node * node = scan.next;
			scan.next = node->m_next;

			if(node->m_data && RefMatchesScan(scan, node->m_data))
				return node->m_data;
		}

		NextScanCell(scan);
	}

	return nullptr;
}

static ParamInfo kParams_GetFirstRef[4] =
{
	{	"form type",			kParamType_Integer,	1 },
	{	"cell depth",			kParamType_Integer,	1 },
	{	"include taken refs",	kParamType_Integer,	1 },
	{	"include deleted refs",	kParamType_Integer,	1 },
};

static bool Cmd_GetFirstRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 formType = 0;
	u32 depth = 0;
	u32 includeTaken = 0;
	u32 includeDeleted = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &formType, &depth, &includeTaken, &includeDeleted)) return true;

	PlayerCharacter * player = PlayerCharacter::Get();
	if(!script || !player || !player->parentCell) return true;

	// Script derives from TESForm
	u32 scriptID = reinterpret_cast <TESForm *>(script)->refID;

	CellRefScan & scan = s_cellRefScans[scriptID];
	StartCellRefScan(scan, player->parentCell, formType, depth, includeTaken != 0, includeDeleted != 0);

	TESObjectREFR * refr = ContinueCellRefScan(scan);
	if(refr)
		SetFormResult(result, refr->refID);
	else
		s_cellRefScans.erase(scriptID);

	if(IsConsoleMode())
		Console_Print("GetFirstRef >> %08X", refr ? refr->refID : 0);

	return true;
}

static bool Cmd_GetNextRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!script) return true;

	// Script derives from TESForm
	auto scan = s_cellRefScans.find(reinterpret_cast <TESForm *>(script)->refID);
	if(scan == s_cellRefScans.end()) return true;

	TESObjectREFR * refr = (scan->second.gameLoadCount == s_gameLoadCount) ? ContinueCellRefScan(scan->second) : nullptr;
	if(refr)
		SetFormResult(result, refr->refID);
	else
		s_cellRefScans.erase(scan);

	if(IsConsoleMode())
		Console_Print("GetNextRef >> %08X", refr ? refr->refID : 0);

	return true;
}

static CommandInfo kCommandInfo_GetFirstRef =
{
	"GetFirstRef", "",
	0,
	"returns the first reference of the specified type in the current cell",
	0,
	4, kParams_GetFirstRef,
	Cmd_GetFirstRef_Execute
};

static CommandInfo kCommandInfo_GetNextRef =
{
	"GetNextRef", "",
	0,
	"returns the next reference of a given type in the current cell",
	0,
	0, nullptr,
	Cmd_GetNextRef_Execute
};

// debug output, switched on per mod by SetDebugMode
static u32 s_modDebugStates[8];

static u8 GetScriptModIndex(Script * script)
{
	// Script derives from TESForm
	return script ? u8(reinterpret_cast <TESForm *>(script)->refID >> 24) : 0xFF;
}

static bool IsDebugModeOn(Script * script)
{
	u8 modIndex = GetScriptModIndex(script);
	return (s_modDebugStates[modIndex / 32] & (1 << (modIndex % 32))) != 0;
}

static bool Cmd_SetDebugMode_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 enable = 0;
	u32 modIndexArg = 0xFFFF;
	if(!ExtractArgs(EXTRACT_ARGS, &enable, &modIndexArg)) return true;

	u8 modIndex = (modIndexArg == 0xFFFF) ? GetScriptModIndex(script) : u8(modIndexArg);
	if(modIndex > 0 && modIndex < 0xFF)
	{
		u32 bit = 1 << (modIndex % 32);
		if(enable)
			s_modDebugStates[modIndex / 32] |= bit;
		else
			s_modDebugStates[modIndex / 32] &= ~bit;

		if(IsConsoleMode())
			Console_Print("Debug statements toggled %s for mod %02X", enable ? "on" : "off", modIndex);
	}

	return true;
}

static TESForm * FormFromValue(double value)
{
	u32 formID = FormIDFromValue(value);
	return formID ? LookupFormByID(formID) : nullptr;
}

// Many of the Remastered's names are keys that Unreal turns into the text it shows: "LOC_" ones
// from its string tables (LOC_FN_Phintias), "UI_" ones from its table of text the player typed
// (the player's name). 0x04883D70 makes Unreal's text for a key in a std::string, through
// 0x04883EA0, which builds its tables the first time (the byte at 0x09306CD0 says it has), and
// FText::ToString (0x00E63E60) gives what it shows. Asked from the thread scripts run on, a "UI_"
// key comes back right, but a "LOC_" key comes back as "<MISSING STRING TABLE ENTRY>": those are
// left as they are, and MessageEX and MessageBoxEX have Unreal translate them.
struct GameStdString	// the game's std::string, as the game reads one (MSVC's layout)
{
	union
	{
		char		buf[16];
		const char	* ptr;	// once capacity passes 15
	};
	u64	size;
	u64	capacity;
};

// text, as a std::string the game only reads; it points into text when longer than 15 characters
static GameStdString ToGameStdString(const char * text)
{
	GameStdString str = {};
	str.size = strlen(text);
	if(str.size < sizeof(str.buf))
	{
		memcpy(str.buf, text, str.size + 1);
		str.capacity = sizeof(str.buf) - 1;
	}
	else
	{
		str.ptr = text;
		str.capacity = str.size;
	}

	return str;
}

struct UnrealText	// FText: a thread-safe shared pointer to its data, then flags
{
	void	* data;
	u8		* referenceController;	// shared count at +8, weak count at +0xC
	u32		flags;
	u32		pad;
};

struct UnrealString	// FString
{
	const wchar_t	* data;
	s32				length;	// counting the terminator
	s32				capacity;
};

typedef UnrealText * (* _TextFromKey)(UnrealText * out, const GameStdString * key);
static RelocAddr <_TextFromKey> TextFromKey(0x04883D70);

typedef const UnrealString * (* _Text_ToString)(const UnrealText * text);
static RelocAddr <_Text_ToString> Text_ToString(0x00E63E60);

static RelocPtr <u8> g_textKeyTablesBuilt(0x09306CD0);

static void ReleaseText(UnrealText * text)
{
	u8 * controller = text->referenceController;
	if(!controller) return;

	auto ** vtable = *reinterpret_cast <void ***>(controller);
	if(_InterlockedDecrement(reinterpret_cast <volatile long *>(controller + 8)) == 0)
	{
		reinterpret_cast <void (*)(void *)>(vtable[0])(controller);	// destroys the data
		if(_InterlockedDecrement(reinterpret_cast <volatile long *>(controller + 0xC)) == 0)
			reinterpret_cast <void (*)(void *, u32)>(vtable[1])(controller, 1);	// and itself
	}
}

// A form's name, with a "UI_" key turned into the text the game shows (characters past Latin-1
// become '?'); a "LOC_" key stays a key
static std::string DisplayName(TESForm * form)
{
	const char * name = GetFullName(form);
	if(strncmp(name, "UI_", 3) || !*g_textKeyTablesBuilt) return name;

	GameStdString key = ToGameStdString(name);

	UnrealText text = {};
	TextFromKey(&text, &key);

	std::string shown;
	if(text.data)
	{
		const UnrealString * str = Text_ToString(&text);
		if(str && str->data)
		{
			for(s32 i = 0; i < str->length - 1 && str->data[i]; i++)
				shown += (str->data[i] < 0x100) ? char(str->data[i]) : '?';
		}
	}

	ReleaseText(&text);

	return shown.empty() ? name : shown;
}

// Expands a classic OBSE format string. OBSE's own specifiers are replaced in place, and any
// other % takes the next argument as a number, as classic OBSE left those to sprintf. %n and %i
// take a form, which a variable holds as its form ID's number, and %z a string variable. The ones
// that need a name table (%c %p %k %v) still use up their arguments, so later numbers line up, but
// print "?" for now. False if there are too few arguments; numUsed receives how many it took.
static bool FormatClassicString(const char * fmt, const double * args, u32 numArgs, std::string * out, u32 * numUsed)
{
	u32 argIdx = 0;
	auto nextArg = [&](double * value)
	{
		if(argIdx >= numArgs) return false;
		*value = args[argIdx++];
		return true;
	};

	out->clear();

	for(const char * p = fmt; *p; p++)
	{
		if(*p != '%')
		{
			*out += *p;
			continue;
		}

		double value = 0;

		switch(p[1])
		{
			case '%':	*out += '%';	p++;	break;
			case 'r':
			case 'R':	*out += '\n';	p++;	break;
			case 'q':
			case 'Q':	*out += '"';	p++;	break;
			case 'e':
			case 'E':					p++;	break;	// empty: works around the CS rejecting empty strings
			case 'B':	*out += '\2';	p++;	break;	// blue console text on
			case 'b':	*out += '\3';	p++;	break;	// and off
			case '}':					p++;	break;

			case 'a':
			case 'A':
				if(!nextArg(&value)) return false;
				*out += char(int(value));
				p++;
				break;

			case 'n':
			case 'N':
				// the form's name, as the game shows it
				if(!nextArg(&value)) return false;
				*out += DisplayName(FormFromValue(value));
				p++;
				break;

			case 'i':
			case 'I':
			{
				// the form's ID, zeros for none
				if(!nextArg(&value)) return false;

				TESForm * form = FormFromValue(value);

				char buf[16];
				snprintf(buf, sizeof(buf), "%08X", form ? form->refID : 0);
				*out += buf;
				p++;
			}
			break;

			case 'z':
			case 'Z':
				// the string variable's text, nothing for a missing one
				if(!nextArg(&value)) return false;
				if(const std::string * str = LookupStringVar(u32(s64(value))))
					*out += *str;
				p++;
				break;

			case 'k': case 'K':
			case 'v': case 'V':
				if(!nextArg(&value)) return false;
				*out += '?';
				p++;
				break;

			case 'p':
			case 'P':
				if(!nextArg(&value)) return false;
				*out += '?';
				p++;
				if(p[1]) p++;	// the o, s or p that picks the pronoun
				break;

			case 'c':
			case 'C':
				if(!nextArg(&value) || !nextArg(&value)) return false;
				*out += '?';
				p++;
				break;

			case '{':
				// %{text%} keeps text only if the argument is nonzero
				if(!nextArg(&value)) return false;
				p++;
				if(value == 0)
				{
					const char * close = strstr(p + 1, "%}");
					p = close ? close + 1 : p + strlen(p) - 1;
				}
				break;

			case 'x':
			case 'X':
			{
				if(!nextArg(&value)) return false;
				p++;

				int width = 0;
				if(isdigit((u8)p[1]))	// a single-digit width may follow
					width = *++p - '0';

				char buf[32];
				snprintf(buf, sizeof(buf), "%0*llX", width, (unsigned long long)value);
				*out += buf;
			}
			break;

			default:
			{
				// a printf-style number: flags, width, precision, conversion
				const char * end = p + 1;
				while(*end && strchr("-+ #0", *end)) end++;
				while(isdigit((u8)*end)) end++;
				if(*end == '.')
				{
					end++;
					while(isdigit((u8)*end)) end++;
				}

				if(!nextArg(&value)) return false;

				char buf[128];
				if(*end && strchr("fFgGeE", *end))
				{
					snprintf(buf, sizeof(buf), std::string(p, end + 1).c_str(), value);
					*out += buf;
				}
				else if(*end && strchr("diu", *end))
				{
					std::string spec(p, end);
					spec += (*end == 'u') ? "llu" : "lld";
					snprintf(buf, sizeof(buf), spec.c_str(), (long long)value);
					*out += buf;
				}
				else
				{
					// not a number conversion: print it as written
					out->append(p, *end ? end + 1 : end);
				}

				p = *end ? end : end - 1;
			}
			break;
		}
	}

	*numUsed = argIdx;

	return true;
}

ParamInfo kParams_FormatString[21] =
{
	{	"format string",	kParamType_String,	0 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
};

// Reads a command's format string and its numbers, as classic OBSE's PrintToConsole does: up to
// 20 for the format string, and one more that some commands take after them. The numbers are read
// one at a time by the game's numeric argument reader, which keeps them as doubles, so a ref
// variable's form ID comes through whole: ExtractArgs would round them to floats.
bool ExtractClassicFormatString(COMMAND_ARGS, std::string * out, std::vector <double> * rest)
{
	u32 offset = *opcodeOffsetPtr;
	u32 end = offset + *(const u16 *)(scriptData + offset - 2);	// the arguments' length is just before them

	// the argument count comes first, and includes the format string
	u16 numArgs = *(const u16 *)(scriptData + offset);
	u16 fmtLen = *(const u16 *)(scriptData + offset + 2);
	offset += 4;

	if(!numArgs || numArgs > 22 || offset + fmtLen > end) return false;

	std::string fmt(scriptData + offset, fmtLen);
	offset += fmtLen;

	double args[21] = { 0 };
	u32 numNumbers = numArgs - 1;
	for(u32 i = 0; i < numNumbers; i++)
	{
		if(offset >= end || !ExtractNumericArg(&args[i], scriptData, &offset, thisObj, containingObj, script, locals, true))
			return false;
	}

	*opcodeOffsetPtr = offset;

	u32 numUsed = 0;
	if(!FormatClassicString(fmt.c_str(), args, numNumbers, out, &numUsed)) return false;

	if(rest)
		rest->assign(args + numUsed, args + numNumbers);

	return true;
}

static bool Cmd_DebugPrint_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(IsDebugModeOn(script))
	{
		std::string text;
		if(ExtractClassicFormatString(PASS_COMMAND_ARGS, &text))
			ScriptConsolePrint(text.c_str());
	}

	return true;
}

static bool Cmd_PrintD_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(IsDebugModeOn(script))
	{
		ObseExpression eval(PASS_COMMAND_ARGS);
		if(eval.ExtractArgs() && eval.NumArgs() && eval.Arg(0).type == ObseExpression::Value::kType_String)
			ScriptConsolePrint(eval.Arg(0).string.c_str());
	}

	return true;
}

static bool Cmd_TestExpr_Execute(COMMAND_ARGS)
{
	*result = 0;

	ObseExpression eval(PASS_COMMAND_ARGS);
	if(eval.ExtractArgs() && eval.NumArgs())
		*result = 1;

	return true;
}

// The game's messages for Unreal (0x20 bytes) are a list of parts of 0x38 bytes, each a key and
// named arguments. Unreal turns each part's key into text, formats its arguments into it
// (FText::Format: {name}, with ` before a brace that's meant), and joins the parts. A string
// argument that is a key gets translated there, on Unreal's thread, where its string tables can
// be read (docs/script-engine.md, "Messages"). 0x0655A5A0 adds a part with one string argument,
// as the game does for a disease's name; 0x04722180 destroys the parts. The game's Message and
// MessageBox show such a message as a notification (0x0656C6B0) and a message box (0x06568C40).
struct UIMessage
{
	u32		unk00;
	void	* parts[3];	// begin, end, capacity
};

typedef void (* _UIMessage_AddPart)(UIMessage * message, const GameStdString * key, const char * argName, const char * argValue);
static RelocAddr <_UIMessage_AddPart> UIMessage_AddPart(0x0655A5A0);

typedef void (* _UIMessage_DestroyParts)(void ** parts);
static RelocAddr <_UIMessage_DestroyParts> UIMessage_DestroyParts(0x04722180);

// the message, the callback that records the button pressed, flags, a button of its own (none
// here), then the buttons, up to an empty one
typedef bool (* _ShowMessageBox)(UIMessage * message, uintptr_t callback, u32 flags, const char * ownButton, ...);
static RelocAddr <_ShowMessageBox> ShowMessageBox(0x06568C40);
static RelocAddr <uintptr_t> kMessageBox_RecordButton(0x06A0DF40);

// what GetButtonPressed reads: the form ID of the script's calling reference (or of the script),
// and the button pressed, 0xFF until one is
static RelocPtr <u32> g_messageBoxFormID(0x094800BC);
static RelocPtr <u8> g_messageBoxButton(0x08FAB8F8);

enum
{
	kMaxMessageBoxButtons =	10,
	kRefFlag_Temporary =	0x4000,
};

static bool IsKeyChar(char c)
{
	return isalnum(u8(c)) || c == '_';
}

// text as FText::Format reads it literally
static void AppendFormatEscaped(std::string * out, const std::string & text, size_t start, size_t end)
{
	for(size_t i = start; i < end; i++)
	{
		if(strchr("{}`", text[i]))
			*out += '`';

		*out += text[i];
	}
}

// The text as a message for Unreal: each "LOC_" key in it ends a part, as that part's argument,
// so Unreal shows it translated.
static void BuildUIMessage(UIMessage * message, const std::string & text)
{
	auto addPart = [&](const std::string & pattern, const char * key)
	{
		GameStdString gamePattern = ToGameStdString(pattern.c_str());
		UIMessage_AddPart(message, &gamePattern, "Key", key);
	};

	std::string pattern;
	bool added = false;
	size_t pos = 0;
	for(size_t found = text.find("LOC_"); found != std::string::npos; found = text.find("LOC_", found + 1))
	{
		size_t end = found + 4;
		while(end < text.size() && IsKeyChar(text[end]))
			end++;

		if((found > 0 && IsKeyChar(text[found - 1])) || end == found + 4) continue;

		AppendFormatEscaped(&pattern, text, pos, found);
		pattern += "{Key}";
		addPart(pattern, text.substr(found, end - found).c_str());

		pattern.clear();
		added = true;
		pos = end;
		found = end - 1;
	}

	AppendFormatEscaped(&pattern, text, pos, text.size());
	if(!pattern.empty() || !added)
		addPart(pattern, "");
}

static bool Cmd_MessageEX_Execute(COMMAND_ARGS)
{
	*result = 0;

	std::string text;
	std::vector <double> rest;
	if(!ExtractClassicFormatString(PASS_COMMAND_ARGS, &text, &rest)) return true;

	// classic's optional duration in seconds follows the format string's numbers; without it, the
	// game's Message uses 10
	float duration = (!rest.empty() && rest[0] > 0) ? float(rest[0]) : 10.0f;

	UIMessage message = {};
	BuildUIMessage(&message, text);
	QueueUIMessage(&message, nullptr, 1, duration);
	UIMessage_DestroyParts(message.parts);

	*result = 1;

	return true;
}

static bool Cmd_MessageBoxEX_Execute(COMMAND_ARGS)
{
	*result = 0;

	std::string text;
	if(!ExtractClassicFormatString(PASS_COMMAND_ARGS, &text)) return true;

	// "message|button|button...", up to ten buttons. With none the game adds its own "Done", and an
	// empty one is left out, as the game would end the list of buttons there.
	size_t bar = text.find('|');
	std::string message = text.substr(0, bar);

	std::vector <std::string> buttons;
	while(bar != std::string::npos && buttons.size() < kMaxMessageBoxButtons)
	{
		size_t next = text.find('|', bar + 1);
		std::string button = text.substr(bar + 1, (next == std::string::npos) ? std::string::npos : next - bar - 1);
		if(!button.empty())
			buttons.push_back(button);

		bar = next;
	}

	// for GetButtonPressed, as the game's MessageBox does it: the calling reference, unless it's a
	// temporary one or there's none (a quest's script, an item's), and then the script
	if(thisObj && !(thisObj->flags & kRefFlag_Temporary))
		*g_messageBoxFormID = thisObj->refID;
	else if(script)
		*g_messageBoxFormID = reinterpret_cast <TESForm *>(script)->refID;	// Script derives from TESForm
	else
		return true;

	*g_messageBoxButton = 0xFF;

	const char * b[kMaxMessageBoxButtons + 1] = { };
	for(size_t i = 0; i < buttons.size(); i++)
		b[i] = buttons[i].c_str();

	if(buttons.empty())
		b[0] = "LOC_HC_MenuGamesettings_sDone";

	UIMessage box = {};
	BuildUIMessage(&box, message);
	ShowMessageBox(&box, kMessageBox_RecordButton.getUIntPtr(), 0, nullptr,
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
	UIMessage_DestroyParts(box.parts);

	return true;
}

// The console runs a typed line as classic's RunScriptLine did (0x06555280): a temporary script
// (built by 0x0662E4F0, marked temporary by 0x06612830) takes the line as its text (0x066315B0)
// and is compiled and run (0x0662EBE0) by the compiler at the start of the console manager (from
// 0x066331E0), then destroyed (0x0662E5A0). A line that doesn't compile leaves the script without
// bytecode: its length at +0x38 stays 0. Running it sets and clears the console mode flag.
typedef void (* _Script_Construct)(void * script);
static RelocAddr <_Script_Construct> Script_Construct(0x0662E4F0);

typedef void (* _Script_MarkAsTemporary)(void * script);
static RelocAddr <_Script_MarkAsTemporary> Script_MarkAsTemporary(0x06612830);

typedef void (* _Script_SetText)(void * script, const char * text);
static RelocAddr <_Script_SetText> Script_SetText(0x066315B0);

typedef void (* _Script_CompileAndRun)(void * script, void * compiler, u32 compilerType, TESObjectREFR * callingObj);
static RelocAddr <_Script_CompileAndRun> Script_CompileAndRun(0x0662EBE0);

typedef void (* _Script_Destruct)(void * script);
static RelocAddr <_Script_Destruct> Script_Destruct(0x0662E5A0);

typedef void ** (* _ConsoleManager_Get)(bool create);
static RelocAddr <_ConsoleManager_Get> ConsoleManager_Get(0x066331E0);

enum
{
	kScriptCompiler_Console =	1,

	kScriptOffset_DataLength =	0x38,
	kScriptBufferSize =			0x100,	// the console keeps its script in 0x98 bytes
};

// compiles and runs one line as the console does; false if it didn't compile
static bool RunScriptLine(const char * text, TESObjectREFR * callingObj)
{
	void ** consoleManager = ConsoleManager_Get(true);
	if(!consoleManager || !*consoleManager) return false;

	alignas(16) u8 scriptBuf[kScriptBufferSize];
	u8 consoleMode = *g_bConsoleMode;

	Script_Construct(scriptBuf);
	Script_MarkAsTemporary(scriptBuf);
	Script_SetText(scriptBuf, text);
	Script_CompileAndRun(scriptBuf, *consoleManager, kScriptCompiler_Console, callingObj);
	bool compiled = *(const u32 *)(scriptBuf + kScriptOffset_DataLength) != 0;
	Script_Destruct(scriptBuf);

	*g_bConsoleMode = consoleMode;

	return compiled;
}

static ParamInfo kParams_RunBatchScript[3] =
{
	{	"filename",			kParamType_String,	0 },
	{	"run on ref",		kParamType_Integer,	1 },
	{	"suppress output",	kParamType_Integer,	1 },
};

// Runs each line of a text file as a console command, as classic did. Classic opened the file
// relative to Oblivion's folder, the one holding Data; the Remastered's is Content\Dev\ObvData,
// two folders up from the exe's, and a path is tried there first. Blank lines and comments are
// skipped instead of being compiled to nothing.
static bool Cmd_RunBatchScript_Execute(COMMAND_ARGS)
{
	*result = 0;

	char fileName[BUFSIZ] = { 0 };
	u32 runOnRef = 0;
	u32 suppressOutput = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &fileName, &runOnRef, &suppressOutput)) return true;

	std::string inDataFolder = getRuntimeDirectory() + "..\\..\\Content\\Dev\\ObvData\\" + fileName;

	FILE * src = nullptr;
	if(fopen_s(&src, inDataFolder.c_str(), "r") && fopen_s(&src, fileName, "r"))
	{
		_MESSAGE("RunBatchScript: can't open %s", fileName);
		return true;
	}

	*result = 1;

	// classic printed what the lines print, unless asked not to
	u8 outputEnabled = *g_bConsoleOutputEnabled;
	*g_bConsoleOutputEnabled = suppressOutput ? 0 : 1;

	char line[4096];
	while(fgets(line, sizeof(line), src))
	{
		size_t len = strlen(line);
		while(len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = 0;

		const char * start = line;
		while(*start == ' ' || *start == '\t')
			start++;

		if(!*start || *start == ';')
			continue;

		if(!RunScriptLine(start, runOnRef ? thisObj : nullptr))
			*result = 0;
	}

	*g_bConsoleOutputEnabled = outputEnabled;

	fclose(src);

	return true;
}

// Classic asked the game's file finder, relative to Oblivion's folder, which also looks in the
// BSA archives. This looks for a loose file only, in Content\Dev\ObvData first, as RunBatchScript
// opens one.
static bool Cmd_FileExists_Execute(COMMAND_ARGS)
{
	*result = 0;

	char fileName[BUFSIZ] = { 0 };
	if(!ExtractArgs(EXTRACT_ARGS, &fileName)) return true;

	std::string inDataFolder = getRuntimeDirectory() + "..\\..\\Content\\Dev\\ObvData\\" + fileName;

	for(const char * path : { inDataFolder.c_str(), (const char *)fileName })
	{
		DWORD attributes = GetFileAttributesA(path);
		if(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			*result = 1;
			break;
		}
	}

	if(IsConsoleMode())
		Console_Print("FileExists >> %d", (int)*result);

	return true;
}

// The game's script runner (0xC0 bytes, built by 0x0694C7C0). ScriptRunner::Run (0x0694D6D0) steps
// through a script's statements and has 0x0694C810 run each one. The runner keeps the if/else
// nesting: a depth, and for each level 1 while inside an if and 2 once one of its branches ran.
struct ScriptRunner
{
	u8		unk00[0x28];	// 00
	Script	* script;		// 28 the script running, 0 when idle
	u32		unk30;			// 30
	u32		unk34;			// 34
	s32		ifDepth;		// 38
	u32		ifStack[10];	// 3C
};

static_assert(offsetof(ScriptRunner, script) == 0x28);
static_assert(offsetof(ScriptRunner, ifDepth) == 0x38);
static_assert(offsetof(ScriptRunner, ifStack) == 0x3C);

// the runner Run's callers use, unless it's busy with another script (0x0694D580)
static RelocPtr <ScriptRunner *> g_scriptRunner(0x094904E8);

// Run moves to the next statement by adding the statement's length to its arguments' offset. It
// passes the length to 0x0694C810 by pointer, as the fifth argument, which a command can change to
// jump, as classic's Goto did. 0x0694C810 calls commands from 0x0694D179 with opcodeOffsetPtr at its
// rsp + 0x40, and its fifth argument is at rsp + 0x800 (eight pushes, 0x798 of locals, the return
// address and four home slots).
static RelocAddr <uintptr_t> kStatementCommandReturn(0x0694D17C);
static const u32 kNextStatementLengthOffset = 0x800 - 0x40;

// where Label was, and the nesting there, by script and label
struct SavedIP
{
	u32	ip;
	s32	ifDepth;
	u32	ifStack[10];
};

static std::map <std::pair <Script *, u32>, SavedIP> s_savedIPs;

// The runner running this statement, and Run's length for it. False unless the command is a
// statement of its own run by the main runner: not part of an expression, nor in a script that
// another one started.
static bool GetStatementState(void * returnAddress, Script * script, const char * scriptData, u32 argsOffset,
	u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length)
{
	if(uintptr_t(returnAddress) != kStatementCommandReturn.getUIntPtr()) return false;

	*runner = *g_scriptRunner;
	if(!*runner || (*runner)->script != script) return false;

	// the length Run read, which is just before the arguments
	*length = *(s32 **)((u8 *)opcodeOffsetPtr + kNextStatementLengthOffset);
	return *length && **length == *(const s16 *)(scriptData + argsOffset - 2);
}

static void ReportUnfollowedJump(const char * name, Script * script)
{
	static bool s_reported = false;
	if(!s_reported)
	{
		s_reported = true;
		_MESSAGE("%s in script %08X ignored: it only works as a statement of its own, in a script the game runs itself",
			name, script ? reinterpret_cast <TESForm *>(script)->refID : 0);
	}
}

static bool Cmd_SaveIP_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	u32 label = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &label)) return true;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportUnfollowedJump("Label", script);
		return true;
	}

	SavedIP & saved = s_savedIPs[{ script, label }];
	saved.ip = argsOffset + *length;
	saved.ifDepth = runner->ifDepth;
	memcpy(saved.ifStack, runner->ifStack, sizeof(saved.ifStack));

	return true;
}

static bool Cmd_RestoreIP_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	u32 label = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &label)) return true;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportUnfollowedJump("Goto", script);
		return true;
	}

	// classic jumped to the start of the script for a label it hadn't seen; do nothing instead
	auto saved = s_savedIPs.find({ script, label });
	if(saved == s_savedIPs.end()) return true;

	*length = s32(saved->second.ip) - s32(argsOffset);
	runner->ifDepth = saved->second.ifDepth;
	memcpy(runner->ifStack, saved->second.ifStack, sizeof(runner->ifStack));

	return true;
}

bool Cmd_Expression_Parse(u32 numParams, const ParamInfo * paramInfo, ScriptLineBuffer * lineBuf, ScriptBuffer * scriptBuf)
{
	Console_Print("OBSE64 can't compile OBSE expressions yet, so this command only works in compiled scripts");
	return false;
}

static ParamInfo kParams_OneInt_OneOptionalInt[2] =
{
	{	"int", kParamType_Integer, 0 },
	{	"int", kParamType_Integer, 1 },
};

static CommandInfo kCommandInfo_SetDebugMode =
{
	"SetDebugMode", "",
	0,
	"toggles debug mode for all scripts belonging to a mod",
	0,
	2, kParams_OneInt_OneOptionalInt,
	Cmd_SetDebugMode_Execute
};

static CommandInfo kCommandInfo_DebugPrint =
{
	"DebugPrint", "",
	0,
	"prints a formatted string to the console if debug mode is enabled for the calling script",
	0,
	21, kParams_FormatString,
	Cmd_DebugPrint_Execute
};

static CommandInfo kCommandInfo_PrintD =
{
	"PrintD", "",
	0,
	"prints a string expression to the console if debug mode is enabled for the calling script",
	0,
	0, nullptr,
	Cmd_PrintD_Execute,
	Cmd_Expression_Parse
};

static CommandInfo kCommandInfo_TestExpr =
{
	"TestExpr", "",
	0,
	"returns 1 if the expression evaluates without an error",
	0,
	0, nullptr,
	Cmd_TestExpr_Execute,
	Cmd_Expression_Parse
};

static CommandInfo kCommandInfo_MessageEX =
{
	"MessageEX", "",
	0,
	"Prints a formatted message",
	0,
	21, kParams_FormatString,
	Cmd_MessageEX_Execute
};

static CommandInfo kCommandInfo_MessageBoxEX =
{
	"MessageBoxEX", "",
	0,
	"Displays a formatted messagebox",
	0,
	21, kParams_FormatString,
	Cmd_MessageBoxEX_Execute
};

static ParamInfo kParams_OneString[1] =
{
	{	"string", kParamType_String, 0 },
};

static CommandInfo kCommandInfo_FileExists =
{
	"FileExists", "",
	0,
	"returns true if the specified file exists",
	0,
	1, kParams_OneString,
	Cmd_FileExists_Execute
};

static CommandInfo kCommandInfo_RunBatchScript =
{
	"RunBatchScript", "",
	0,
	"Similar to the 'bat' console command",
	0,
	3, kParams_RunBatchScript,
	Cmd_RunBatchScript_Execute
};

static ParamInfo kParams_OneOptionalInt[1] =
{
	{	"int", kParamType_Integer, 1 },
};

static ParamInfo kParams_SetObjectInteger[2] =
{
	{	"value", kParamType_Integer, 0 },
	{	"type", kParamType_InventoryObject, 1 },
};

static ParamInfo kParams_SetObjectString[2] =
{
	{	"value", kParamType_String, 0 },
	{	"type", kParamType_InventoryObject, 1 },
};

static ParamInfo kParams_CopyObjectPath[2] =
{
	{	"src", kParamType_InventoryObject, 0 },
	{	"dest", kParamType_InventoryObject, 1 },
};

static CommandInfo kCommandInfo_IsQuestItem =
{
	"IsQuestItem", "",
	0,
	"returns 1 if the object or calling reference is a quest item",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_IsQuestItem_Execute
};

static CommandInfo kCommandInfo_SetQuestItem =
{
	"SetQuestItem", "",
	0,
	"Sets whether the specified object or object reference is a quest item.",
	0,
	2, kParams_SetObjectInteger,
	Cmd_SetQuestItem_Execute
};

static CommandInfo kCommandInfo_CompareModelPath =
{
	"CompareModelPath", "",
	0,
	"Returns 1 if the given string is part of the model path.",
	0,
	2, kParams_SetObjectString,
	Cmd_CompareModelPath_Execute
};

static CommandInfo kCommandInfo_CompareMaleGroundPath =
{
	"CompareMaleGroundPath", "",
	0,
	"Returns 1 if the given string is part of the male ground path.",
	0,
	2, kParams_SetObjectString,
	Cmd_CompareMaleGroundPath_Execute
};

static CommandInfo kCommandInfo_CopyModelPath =
{
	"CopyModelPath", "",
	0,
	"copies the model path from one object to another",
	0,
	2, kParams_CopyObjectPath,
	Cmd_CopyModelPath_Execute
};

static CommandInfo kCommandInfo_CopyMaleGroundPath =
{
	"CopyMaleGroundPath", "",
	0,
	"copies the male ground path from one object to another",
	0,
	2, kParams_CopyObjectPath,
	Cmd_CopyMaleGroundPath_Execute
};

static CommandInfo kCommandInfo_SaveIP =
{
	"SaveIP", "",
	0,
	"marks a place in the script for RestoreIP (Goto) to jump back to",
	0,
	1, kParams_OneOptionalInt,
	Cmd_SaveIP_Execute
};

static CommandInfo kCommandInfo_RestoreIP =
{
	"RestoreIP", "",
	0,
	"jumps back to the place SaveIP (Label) marked",
	0,
	1, kParams_OneOptionalInt,
	Cmd_RestoreIP_Execute
};

static CommandInfo kCommandInfo_GetActiveMenuSelection =
{
	"GetActiveMenuSelection", "",
	0,
	"returns the selected item in the active menu",
	0,
	1, kParams_OneOptionalInt,
	Cmd_GetActiveMenuSelection_Execute
};

static CommandInfo kCommandInfo_GetGameLoaded =
{
	"GetGameLoaded", "",
	0,
	"returns 1 the first time the calling script asks after a game is loaded",
	0,
	0, nullptr,
	Cmd_GetGameLoaded_Execute
};

static CommandInfo kCommandInfo_GetGameRestarted =
{
	"GetGameRestarted", "",
	0,
	"returns 1 the first time the calling script asks after the game starts",
	0,
	0, nullptr,
	Cmd_GetGameRestarted_Execute
};

struct PortedCommand
{
	const CommandInfo	* info;
	CommandReturnType	retnType;
};

// matched to their classic slot by name; the slot supplies the classic name and alias
static const PortedCommand kPortedCommands[] =
{
	{ &kCommandInfo_GetBaseObject,	kRetnType_Form },
	{ &kCommandInfo_GetCrosshairRef,	kRetnType_Form },
	{ &kCommandInfo_IsControlPressed,	kRetnType_Default },
	{ &kCommandInfo_OnControlDown,	kRetnType_Default },
	{ &kCommandInfo_GetOwner,		kRetnType_Form },
	{ &kCommandInfo_GetParentCellOwner,	kRetnType_Form },
	{ &kCommandInfo_GetOwningFactionRequiredRank,	kRetnType_Default },
	{ &kCommandInfo_GetParentCellOwningFactionRequiredRank,	kRetnType_Default },
	{ &kCommandInfo_IsOffLimits,	kRetnType_Default },
	{ &kCommandInfo_GetGoldValue,	kRetnType_Default },
	{ &kCommandInfo_IsRefEssential,	kRetnType_Default },
	{ &kCommandInfo_SetRefEssential,	kRetnType_Default },
	{ &kCommandInfo_GetObjectCharge,	kRetnType_Default },
	{ &kCommandInfo_GetEquippedCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_ModEquippedCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_IsQuestItem,	kRetnType_Default },
	{ &kCommandInfo_SetQuestItem,	kRetnType_Default },
	{ &kCommandInfo_CompareModelPath,	kRetnType_Default },
	{ &kCommandInfo_CompareMaleGroundPath,	kRetnType_Default },
	{ &kCommandInfo_CopyModelPath,	kRetnType_Default },
	{ &kCommandInfo_CopyMaleGroundPath,	kRetnType_Default },
	{ &kCommandInfo_SaveIP,			kRetnType_Default },
	{ &kCommandInfo_RestoreIP,		kRetnType_Default },
	{ &kCommandInfo_GetActiveMenuSelection,	kRetnType_Form },
	{ &kCommandInfo_GetGameLoaded,	kRetnType_Default },
	{ &kCommandInfo_AddItemNS,		kRetnType_Default },
	{ &kCommandInfo_GetFirstRef,	kRetnType_Form },
	{ &kCommandInfo_GetNextRef,		kRetnType_Form },
	{ &kCommandInfo_GetObjectType,	kRetnType_Default },
	{ &kCommandInfo_IsWeapon,		kRetnType_Default },
	{ &kCommandInfo_IsAmmo,			kRetnType_Default },
	{ &kCommandInfo_IsArmor,		kRetnType_Default },
	{ &kCommandInfo_IsClothing,		kRetnType_Default },
	{ &kCommandInfo_IsBook,			kRetnType_Default },
	{ &kCommandInfo_IsIngredient,	kRetnType_Default },
	{ &kCommandInfo_IsContainer,	kRetnType_Default },
	{ &kCommandInfo_IsKey,			kRetnType_Default },
	{ &kCommandInfo_IsAlchemyItem,	kRetnType_Default },
	{ &kCommandInfo_IsApparatus,	kRetnType_Default },
	{ &kCommandInfo_IsSoulGem,		kRetnType_Default },
	{ &kCommandInfo_IsSigilStone,	kRetnType_Default },
	{ &kCommandInfo_IsDoor,			kRetnType_Default },
	{ &kCommandInfo_IsActivator,	kRetnType_Default },
	{ &kCommandInfo_IsLight,		kRetnType_Default },
	{ &kCommandInfo_IsMiscItem,		kRetnType_Default },
	{ &kCommandInfo_IsFurniture,	kRetnType_Default },
	{ &kCommandInfo_IsFlora,		kRetnType_Default },
	{ &kCommandInfo_IsCreature,		kRetnType_Default },
	{ &kCommandInfo_SetDebugMode,	kRetnType_Default },
	{ &kCommandInfo_DebugPrint,		kRetnType_Default },
	{ &kCommandInfo_PrintD,			kRetnType_Default },
	{ &kCommandInfo_TestExpr,		kRetnType_Default },
	{ &kCommandInfo_MessageEX,		kRetnType_Default },
	{ &kCommandInfo_MessageBoxEX,	kRetnType_Default },
	{ &kCommandInfo_RunBatchScript,	kRetnType_Default },
	{ &kCommandInfo_sv_Construct,	kRetnType_String },
	{ &kCommandInfo_sv_Set,			kRetnType_Default },
	{ &kCommandInfo_sv_Length,		kRetnType_Default },
	{ &kCommandInfo_sv_SubString,	kRetnType_String },
	{ &kCommandInfo_GetRider,		kRetnType_Form },
	{ &kCommandInfo_GetGameRestarted,	kRetnType_Default },
	{ &kCommandInfo_FileExists,		kRetnType_Default },
	{ &kCommandInfo_EquipItemNS,	kRetnType_Default },
	{ &kCommandInfo_UnequipItemNS,	kRetnType_Default },
	{ &kCommandInfo_SetEventHandler,	kRetnType_Default },
	{ &kCommandInfo_RemoveEventHandler,	kRetnType_Default },
};

static bool Cmd_ClassicStub_Execute(COMMAND_ARGS)
{
	static std::set <u32> s_reported;

	// the opcode is the u16 four bytes before this command's parameters
	u32 opcode = (scriptData && *opcodeOffsetPtr >= 4) ? *(const u16 *)(scriptData + *opcodeOffsetPtr - 4) : 0;

	if(s_reported.insert(opcode).second)
	{
		CommandInfo * cmd = GetCommandInfo(opcode);
		const char * name = (cmd && cmd->name && *cmd->name) ? cmd->name : "<removed classic command>";

		// Script derives from TESForm
		u32 scriptID = script ? reinterpret_cast <TESForm *>(script)->refID : 0;

		_MESSAGE("classic command %s (%04X) called by script %08X is not implemented yet", name, opcode, scriptID);
		Console_Print("OBSE64: %s is not implemented yet (script %08X)", name, scriptID);
	}

	return false;
}

static const CommandInfo kClassicStubCommand =
{
	"", "",
	0,
	"classic OBSE command that is not implemented in OBSE64 yet",
	0,
	0, nullptr,
	Cmd_ClassicStub_Execute
};

struct ClassicSlot
{
	u32			opcode;
	const char	* name;
	const char	* shortName;
};

static const ClassicSlot kClassicSlots[] =
{
#define CLASSIC(opcode, name, shortName) { opcode, name, shortName },
#include "ClassicCommandTable.inl"
#undef CLASSIC
};

void AddClassicCommands()
{
	const auto & first = kClassicSlots[0];
	const auto & last = kClassicSlots[sizeof(kClassicSlots) / sizeof(kClassicSlots[0]) - 1];

	if(GetNextScriptOpcode() != first.opcode)
	{
		_ERROR("classic commands start at opcode %04X but the command table is at %04X, not adding them",
			first.opcode, GetNextScriptOpcode());
		return;
	}

	// vanilla commands that return a form, as classic OBSE records them: PlaceAtMe, GetActionRef,
	// GetSelf, GetContainer, GetCombatTarget, GetPackageTarget, GetParentRef, CreateFullActorCopy
	static const u32 kVanillaFormCommands[] = { 0x1025, 0x10CD, 0x10CE, 0x10CF, 0x10E8, 0x10E9, 0x1113, 0x1167 };
	for(u32 opcode : kVanillaFormCommands)
		SetCommandReturnType(opcode, kRetnType_Form);

	u32 numPorted = 0;

	for(const auto & slot : kClassicSlots)
	{
		CommandInfo info = kClassicStubCommand;
		CommandReturnType retnType = kRetnType_Default;

		for(const auto & ported : kPortedCommands)
		{
			if(*slot.name && !_stricmp(ported.info->name, slot.name))
			{
				info = *ported.info;
				retnType = ported.retnType;
				numPorted++;
				break;
			}
		}

		info.name = slot.name;
		info.shortName = slot.shortName;

		AddScriptCommand(info, retnType);
	}

	_MESSAGE("classic commands %04X-%04X added, %d ported", first.opcode, last.opcode, numPorted);

	InstallMagicMenuHooks();
	InstallGameLoadedHooks();
	InstallItemMessageHook();
	InstallStringVarHooks();
	InstallUserFunctionBlock();
}
