#include "Commands_Classic.h"
#include "CommandTable.h"
#include "FormatString.h"
#include "FunctionScripts.h"
#include "GameConsole.h"
#include "GameExtraData.h"
#include "GameForms.h"
#include "GameMenus.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "PlayerControls.h"
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

	// one Unreal handles itself
	bool down = false;
	if(PlayerControls_GetDown(control, &down)) return down;

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

static CommandInfo kCommandInfo_HasSpell =
{
	"HasSpell", "hspl",
	0,
	"returns 1 if the actor has the spell",
	1,
	1, kParams_OneSpellItem,
	Cmd_HasSpell_Execute
};

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

// in Commands_Inventory.cpp
extern CommandInfo kCommandInfo_GetBaseObject;
extern CommandInfo kCommandInfo_GetObjectType;
extern CommandInfo kCommandInfo_IsWeapon;
extern CommandInfo kCommandInfo_IsAmmo;
extern CommandInfo kCommandInfo_IsArmor;
extern CommandInfo kCommandInfo_IsClothing;
extern CommandInfo kCommandInfo_IsBook;
extern CommandInfo kCommandInfo_IsIngredient;
extern CommandInfo kCommandInfo_IsContainer;
extern CommandInfo kCommandInfo_IsKey;
extern CommandInfo kCommandInfo_IsAlchemyItem;
extern CommandInfo kCommandInfo_IsApparatus;
extern CommandInfo kCommandInfo_IsSoulGem;
extern CommandInfo kCommandInfo_IsSigilStone;
extern CommandInfo kCommandInfo_IsDoor;
extern CommandInfo kCommandInfo_IsActivator;
extern CommandInfo kCommandInfo_IsLight;
extern CommandInfo kCommandInfo_IsMiscItem;
extern CommandInfo kCommandInfo_IsFurniture;
extern CommandInfo kCommandInfo_IsFlora;
extern CommandInfo kCommandInfo_GetGoldValue;
extern CommandInfo kCommandInfo_IsQuestItem;
extern CommandInfo kCommandInfo_SetQuestItem;
extern CommandInfo kCommandInfo_CompareModelPath;
extern CommandInfo kCommandInfo_CompareMaleGroundPath;
extern CommandInfo kCommandInfo_CopyModelPath;
extern CommandInfo kCommandInfo_CopyMaleGroundPath;
extern CommandInfo kCommandInfo_AddItemNS;
extern CommandInfo kCommandInfo_RemoveItemNS;
extern CommandInfo kCommandInfo_EquipItemNS;
extern CommandInfo kCommandInfo_UnequipItemNS;
extern CommandInfo kCommandInfo_GetObjectCharge;
extern CommandInfo kCommandInfo_GetEquippedCurrentCharge;
extern CommandInfo kCommandInfo_ModEquippedCurrentCharge;
extern CommandInfo kCommandInfo_GetCurrentCharge;
extern CommandInfo kCommandInfo_SetCurrentCharge;
extern CommandInfo kCommandInfo_ModCurrentCharge;
extern CommandInfo kCommandInfo_SetEquippedCurrentCharge;
extern CommandInfo kCommandInfo_GetObjectHealth;
extern CommandInfo kCommandInfo_GetEquippedCurrentHealth;
extern CommandInfo kCommandInfo_SetEquippedCurrentHealth;
extern CommandInfo kCommandInfo_ModEquippedCurrentHealth;
extern CommandInfo kCommandInfo_GetCurrentHealth;
extern CommandInfo kCommandInfo_SetCurrentHealth;
extern CommandInfo kCommandInfo_GetEquippedWeaponPoison;
extern CommandInfo kCommandInfo_SetEquippedWeaponPoison;
extern CommandInfo kCommandInfo_RemoveEquippedWeaponPoison;
extern CommandInfo kCommandInfo_GetHotKeyItem;
extern CommandInfo kCommandInfo_ClearHotKey;
extern CommandInfo kCommandInfo_SetHotKeyItem;

// in Commands_Game.cpp
extern CommandInfo kCommandInfo_GetCrosshairRef;
extern CommandInfo kCommandInfo_GetGameLoaded;
extern CommandInfo kCommandInfo_GetGameRestarted;
extern CommandInfo kCommandInfo_MessageEX;
extern CommandInfo kCommandInfo_MessageBoxEX;

// in Commands_Loops.cpp, with the loops they keep
extern CommandInfo kCommandInfo_ForEach;
extern CommandInfo kCommandInfo_Loop;
extern CommandInfo kCommandInfo_Break;
extern CommandInfo kCommandInfo_Continue;

// in Commands_String.cpp, with the strings they keep
extern CommandInfo kCommandInfo_sv_Construct;
extern CommandInfo kCommandInfo_sv_Set;
extern CommandInfo kCommandInfo_sv_Length;
extern CommandInfo kCommandInfo_sv_SubString;
extern CommandInfo kCommandInfo_sv_Destruct;

// in Commands_Events.cpp, with the handlers they keep
extern CommandInfo kCommandInfo_SetEventHandler;
extern CommandInfo kCommandInfo_RemoveEventHandler;

// in Commands_Input.cpp, with OBSE64's own key commands
extern CommandInfo kCommandInfo_GetNumKeysPressed;
extern CommandInfo kCommandInfo_GetKeyPress;

// in FunctionScripts.cpp, with the calls they make
extern CommandInfo kCommandInfo_Call;
extern CommandInfo kCommandInfo_SetFunctionValue;

static CommandInfo kCommandInfo_IsOffLimits =
{
	"IsOffLimits", "",
	0,
	"returns true if activating the calling reference would result in a crime for the actor",
	1,
	1, kParams_OneOptionalNPC,
	Cmd_IsOffLimits_Execute
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

// Classic disabled and tapped the key a control was bound to. The Remastered's bindings are
// Unreal's, so these act on the control itself, where it's one Unreal handles on its own
// (PlayerControls.cpp): movement, Run (sprinting) and Always Run (the walk toggle), which is the
// only one that can be tapped. For any other, they do nothing, and say so once.
static void ReportControlCommand(const char * command, u32 control)
{
	static std::set <std::string> s_reported;

	char key[64];
	sprintf_s(key, "%s %u", command, control);

	if(s_reported.insert(key).second)
		_MESSAGE("%s %u: not supported for that control yet", command, control);
}

static bool Cmd_DisableControl_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(!PlayerControls_SetDisabled(control, true))
		ReportControlCommand("DisableControl", control);

	return true;
}

static bool Cmd_EnableControl_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(!PlayerControls_SetDisabled(control, false))
		ReportControlCommand("EnableControl", control);

	return true;
}

// 1 if the control was tapped
static bool Cmd_TapControl_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(PlayerControls_Tap(control))
		*result = 1;
	else
		ReportControlCommand("TapControl", control);

	return true;
}

static CommandInfo kCommandInfo_DisableControl =
{
	"DisableControl", "",
	0,
	"disables the key associated with the specified control",
	0,
	1, kParams_OneInt,
	Cmd_DisableControl_Execute
};

static CommandInfo kCommandInfo_EnableControl =
{
	"EnableControl", "",
	0,
	"enables the key associated with the specified control",
	0,
	1, kParams_OneInt,
	Cmd_EnableControl_Execute
};

static CommandInfo kCommandInfo_TapControl =
{
	"TapControl", "",
	0,
	"taps the key associated with the specified control",
	0,
	1, kParams_OneInt,
	Cmd_TapControl_Execute
};

static ParamInfo kParams_OneOptionalActorBase[1] =
{
	{	"actor base", kParamType_ActorBase, 1 },
};

static bool Cmd_IsCreature_Execute(COMMAND_ARGS)
{
	return IsFormType_Execute(PASS_COMMAND_ARGS, kFormType_Creature, "IsCreature", false);
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

// The game's IsRunning reads an actor's movement flags. Classic set the player's running flag
// whenever Always Run was on (or Run held), moving or not, and mods ask IsRunning whether the
// player runs: No Backwards Running does, to know whether to switch to walking as the player backs
// up. The Remastered's flags for the player come from Unreal, and say running only while moving at
// a run. So for the player on foot, IsRunning in scripts answers as classic did, from Unreal's walk
// toggle and sprinting (PlayerControls.cpp); other actors, the player on a horse, and the game's
// conditions get the game's answer.
enum
{
	kOpcode_IsRunning =	0x111F,
};

static ExecuteFunction s_isRunning = nullptr;

static bool Cmd_IsRunning_Execute(COMMAND_ARGS)
{
	auto * player = reinterpret_cast <TESObjectREFR *>(PlayerCharacter::Get());
	bool running = false;

	if(thisObj && thisObj == player && !*(TESObjectREFR **)((u8 *)player + kActorOffset_HorseOrRider) &&
		PlayerControls_GetRunning(&running))
	{
		*result = running ? 1 : 0;

		if(IsConsoleMode())
			Console_Print(running ? "%s is running" : "%s is not running", GetFullName(player));

		return true;
	}

	return s_isRunning(PASS_COMMAND_ARGS);
}

static void InstallIsRunning()
{
	CommandInfo * info = GetCommandInfo(kOpcode_IsRunning);
	if(!info || !info->execute)
	{
		_ERROR("IsRunning isn't where expected; it keeps the game's answer for the player");
		return;
	}

	s_isRunning = info->execute;
	info->execute = Cmd_IsRunning_Execute;
}

enum
{
	// classic's tile values for a menu list's items: what kind of item, and its index
	kTileValue_user7 =	0xFB5,
	kTileValue_user13 =	0xFBB,
};

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
// so the selection is only given while the list still has the same spell at the hovered index.
typedef void (* _MagicMenu_Hover)(u8 * menu, s32 index);
static RelocAddr <_MagicMenu_Hover> MagicMenu_Hover(0x06774440);
static RelocAddr <uintptr_t> kMagicMenuHover_HoverCall(0x0656F4C1);
static RelocAddr <uintptr_t> kMagicMenuHover_ClickCall(0x0656F448);

typedef u8 * (* _MagicMenu_Construct)(u8 * menu);
static RelocAddr <_MagicMenu_Construct> MagicMenu_Construct(0x067731D0);
static RelocAddr <uintptr_t> kMagicMenu_ConstructCall(0x065DD337);

typedef u8 * (* _MagicMenu_GetItemTile)(u8 * menu, s32 index);
static RelocAddr <_MagicMenu_GetItemTile> MagicMenu_GetItemTile(0x06774A40);

struct MagicMenuListNode
{
	TESForm				* data;
	MagicMenuListNode	* next;
};

static u8 * s_hoveredMagicMenu = nullptr;
static s32 s_hoveredMagicIndex = 0;
static TESForm * s_hoveredMagicSpell = nullptr;	// only compared, never read
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
	s_hoveredMagicSpell = spell ? node->data : nullptr;
	s_hoveredMagicSpellID = spell ? node->data->refID : 0;
}

static u8 * MagicMenu_Construct_Hook(u8 * menu)
{
	s_hoveredMagicMenu = nullptr;

	return MagicMenu_Construct(menu);
}

// The hovered spell's form ID, while the list still holds that spell where it was hovered. The
// spell is compared by its address, not its node, since the first row's node is the menu's own
// whatever spell it holds; and the ID kept from the hover is given, so a spell freed since isn't
// read.
static u32 GetMagicMenuSelection()
{
	u8 * menu = GetMenuByType(kMenuType_Magic);
	if(!menu || menu != s_hoveredMagicMenu || !menu[0xAC] || !s_hoveredMagicSpell) return 0;

	MagicMenuListNode * node = GetMagicMenuSpellNode(menu, s_hoveredMagicIndex);
	if(!node || node->data != s_hoveredMagicSpell) return 0;

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

// GetFirstRef and GetNextRef walk the references in the player's cell, and in an exterior the
// cells around it to a given depth, one matching reference per call, as classic did. A cell keeps
// its references in the list at +0xB8. An exterior cell (flag 1 of +0x79 clear) has its grid
// coordinates behind the pointer at +0xA0, and its worldspace finds a neighbour by them
// (0x06720A00, which only looks in the worldspace's map of cells, so a cell the game hasn't made
// yet is skipped, not made).
//
// The scan reads a cell's list once, as it comes to the cell, and keeps its references' form IDs
// rather than a place in the list, since the list's nodes don't stay put. A reference leaving the
// cell (0x06660170) frees its node, or for the cell's first reference copies the second into the
// first node, which the cell holds, and frees the second; and scripts move the references they're
// given, or go on with a scan in a later frame. Classic kept the node of the reference it gave
// last, and OBSE64 the node after it, and either could be freed before the next call. Each
// reference is now looked up when its turn comes, and passed over if it's gone or has left the
// cell. One that comes into the cell after the scan has read it isn't given, as a walk never gave
// one: the game adds a reference to the front of the list (0x06655BE0, with 0x06579740), where the
// walk has been.
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
	TESWorldSpace		* world;		// set for an exterior scan of the cells around the player's
	s32					centerX;
	s32					centerY;
	s32					depth;
	s32					dx;				// the cell being walked, from the centre
	s32					dy;
	TESObjectCELL		* cell;			// the cell being walked, none when the scan is done
	std::vector <u32>	refIDs;			// its references when the scan came to it
	size_t				next;			// the next of them to look at
	u32					formType;
	bool				includeTaken;
	bool				includeDeleted;
	u32					gameLoadCount;	// a scan started before a load would look in cells that are gone
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
	scan.refIDs.clear();
	scan.next = 0;

	for(auto * node = &cell->objectList.node; node; node = node->m_next)
	{
		if(node->m_data && node->m_data->refID)
			scan.refIDs.push_back(node->m_data->refID);
	}
}

// on to the next cell of the square around the centre that the game has made, row by row
static void NextScanCell(CellRefScan & scan)
{
	scan.cell = nullptr;
	scan.refIDs.clear();
	scan.next = 0;

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
	scan.gameLoadCount = GetGameLoadCount();

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

// the next matching reference still in the cell the scan found it in, none once the scan is done
static TESObjectREFR * ContinueCellRefScan(CellRefScan & scan)
{
	while(scan.cell)
	{
		while(scan.next < scan.refIDs.size())
		{
			TESForm * form = LookupFormByID(scan.refIDs[scan.next++]);
			auto * refr = form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr;

			if(refr && refr->parentCell == scan.cell && RefMatchesScan(scan, refr))
				return refr;
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

	// classic's depth when none is given, so a script giving it means the player's cell alone
	if(depth == 0xFFFFFFFF)
		depth = 0;

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

	TESObjectREFR * refr = (scan->second.gameLoadCount == GetGameLoadCount()) ? ContinueCellRefScan(scan->second) : nullptr;
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

// the runner Run's callers use, unless it's busy with another script (0x0694D580)
static RelocPtr <ScriptRunner *> g_scriptRunner(0x094904E8);

// The runners running a script on this thread, innermost last. Run's caller (0x0694D580) gives it
// the main runner, or a new one when that's busy, as for a function a script calls or a script
// that an Activate starts, and both calls (0x0694D6A8 and 0x0694D645) go through a hook that notes
// the runner while it runs. Run's last five arguments, on the stack, are passed on as they are.
typedef u64 (* _ScriptRunner_Run)(ScriptRunner * runner, void * arg2, void * arg3, void * arg4, u64 arg5,
	u64 arg6, u64 arg7, u64 arg8, u64 arg9);
static RelocAddr <_ScriptRunner_Run> ScriptRunner_Run(0x0694D6D0);
static RelocAddr <uintptr_t> kRun_MainRunnerCall(0x0694D6A8);
static RelocAddr <uintptr_t> kRun_NewRunnerCall(0x0694D645);

static thread_local std::vector <ScriptRunner *> s_runners;
static bool s_runnersNoted = false;

static u64 ScriptRunner_Run_Hook(ScriptRunner * runner, void * arg2, void * arg3, void * arg4, u64 arg5,
	u64 arg6, u64 arg7, u64 arg8, u64 arg9)
{
	s_runners.push_back(runner);
	u64 result = ScriptRunner_Run(runner, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	s_runners.pop_back();

	return result;
}

static void InstallRunnerHooks()
{
	uintptr_t run = ScriptRunner_Run.getUIntPtr();
	if(!IsCallTo(kRun_MainRunnerCall.getUIntPtr(), run) || !IsCallTo(kRun_NewRunnerCall.getUIntPtr(), run))
	{
		_ERROR("the script runner's calls aren't where expected; Goto and loops will work only in scripts the main runner runs");
		return;
	}

	g_branchTrampoline.write5Call(kRun_MainRunnerCall.getUIntPtr(), uintptr_t(ScriptRunner_Run_Hook));
	g_branchTrampoline.write5Call(kRun_NewRunnerCall.getUIntPtr(), uintptr_t(ScriptRunner_Run_Hook));
	s_runnersNoted = true;
}

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

bool IsStatementCall(void * returnAddress)
{
	return uintptr_t(returnAddress) == kStatementCommandReturn.getUIntPtr();
}

bool GetStatementState(void * returnAddress, Script * script, ScriptLocals * locals, const char * scriptData,
	u32 argsOffset, u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length)
{
	if(!IsStatementCall(returnAddress)) return false;

	// The innermost runner, or without the hooks the main one. The main runner can be running this
	// script while another runner runs it too, inside that run (a function calling itself, a
	// script activating another reference with the same script), and putting its nesting back
	// there would leave the inner runner's to grow past its ten levels: so the locals must match.
	if(s_runnersNoted)
		*runner = s_runners.empty() ? nullptr : s_runners.back();
	else
		*runner = *g_scriptRunner;

	if(!*runner || (*runner)->script != script || (*runner)->locals != locals) return false;

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
		_MESSAGE("%s in script %08X ignored: it only works as a statement of its own",
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
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
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
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
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
	Console_Print("xOBSE64 can't compile OBSE expressions yet, so this command only works in compiled scripts");
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
	{ &kCommandInfo_sv_Destruct,	kRetnType_Default },
	{ &kCommandInfo_GetRider,		kRetnType_Form },
	{ &kCommandInfo_GetGameRestarted,	kRetnType_Default },
	{ &kCommandInfo_FileExists,		kRetnType_Default },
	{ &kCommandInfo_EquipItemNS,	kRetnType_Default },
	{ &kCommandInfo_UnequipItemNS,	kRetnType_Default },
	{ &kCommandInfo_SetEventHandler,	kRetnType_Default },
	{ &kCommandInfo_RemoveEventHandler,	kRetnType_Default },
	{ &kCommandInfo_DisableControl,	kRetnType_Default },
	{ &kCommandInfo_EnableControl,	kRetnType_Default },
	{ &kCommandInfo_TapControl,		kRetnType_Default },
	{ &kCommandInfo_GetNumKeysPressed,	kRetnType_Default },
	{ &kCommandInfo_GetKeyPress,	kRetnType_Default },
	{ &kCommandInfo_Call,			kRetnType_Ambiguous },
	{ &kCommandInfo_SetFunctionValue,	kRetnType_Default },
	{ &kCommandInfo_ForEach,		kRetnType_Default },
	{ &kCommandInfo_Loop,			kRetnType_Default },
	{ &kCommandInfo_Break,			kRetnType_Default },
	{ &kCommandInfo_Continue,		kRetnType_Default },
	{ &kCommandInfo_GetCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_SetCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_ModCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_RemoveItemNS,	kRetnType_Default },
	{ &kCommandInfo_HasSpell,		kRetnType_Default },
	{ &kCommandInfo_SetEquippedCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_GetObjectHealth,	kRetnType_Default },
	{ &kCommandInfo_GetEquippedCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_SetEquippedCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_ModEquippedCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_GetCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_SetCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_GetEquippedWeaponPoison,	kRetnType_Form },
	{ &kCommandInfo_SetEquippedWeaponPoison,	kRetnType_Default },
	{ &kCommandInfo_RemoveEquippedWeaponPoison,	kRetnType_Form },
	{ &kCommandInfo_GetHotKeyItem,	kRetnType_Form },
	{ &kCommandInfo_ClearHotKey,	kRetnType_Default },
	{ &kCommandInfo_SetHotKeyItem,	kRetnType_Default },
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

		// shown however the script runs (Console_Print drops lines outside a typed command), and
		// logged above already
		char line[256];
		snprintf(line, sizeof(line), "xOBSE64: %s is not implemented yet (script %08X)", name, scriptID);
		ScriptConsolePrint(line, false);
	}

	return false;
}

static const CommandInfo kClassicStubCommand =
{
	"", "",
	0,
	"classic OBSE command that is not implemented in xOBSE64 yet",
	0,
	0, nullptr,
	Cmd_ClassicStub_Execute
};

// The console command a classic slot named con_ and its name or short name stands for: classic
// OBSE copied some console commands into the script commands so scripts could run them, naming
// each copy so (con_TCL). A copy mustn't take the console command's own name: the game looks a
// name up in the script commands before the console's (0x06948B20), so the console would run the
// copy, or the stub in its place.
static const CommandInfo * FindImportedConsoleCommand(const char * slotName)
{
	if(_strnicmp(slotName, "con_", 4)) return nullptr;

	const char * name = slotName + 4;
	for(u32 i = 0; i < kScript_NumConsoleCommands; i++)
	{
		const CommandInfo & cmd = g_firstConsoleCommand[i];
		if((cmd.name && !_stricmp(name, cmd.name)) || (cmd.shortName && *cmd.shortName && !_stricmp(name, cmd.shortName)))
			return &cmd;
	}

	return nullptr;
}

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

		if(const CommandInfo * console = FindImportedConsoleCommand(slot.name))
		{
			info = *console;
			numPorted++;
		}
		else
		{
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
	PlayerControls_Install();
	InstallIsRunning();
	InstallRunnerHooks();
}
