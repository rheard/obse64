#include "Commands_Classic.h"
#include "CommandTable.h"
#include "FunctionScripts.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "PlayerControls.h"
#include "obse64_common/Log.h"
#include <cstdio>
#include <cstring>
#include <set>

// Classic OBSE commands past release 6, registered at their classic opcodes (from
// ClassicCommandTable.inl) so scripts compiled against classic OBSE call the right
// command. Commands that haven't been ported get a stub that reports itself and fails
// the script line, the same way the unknown opcode failed before.
//
// Each ported command is in the file classic OBSE kept it in, where xOBSE64 has one of that name
// (Commands_Inventory.cpp, Commands_Input.cpp and the rest), so it can be compared with classic's
// there. Classic's loops, user functions and event handlers are in Commands_Loops.cpp,
// FunctionScripts.cpp and Commands_Events.cpp, with what they keep.

// in Commands_Actor.cpp
extern CommandInfo kCommandInfo_IsRefEssential;
extern CommandInfo kCommandInfo_SetRefEssential;
extern CommandInfo kCommandInfo_HasSpell;

// in Commands_Console.cpp
extern CommandInfo kCommandInfo_SetDebugMode;
extern CommandInfo kCommandInfo_DebugPrint;
extern CommandInfo kCommandInfo_PrintD;
extern CommandInfo kCommandInfo_TestExpr;
extern CommandInfo kCommandInfo_RunBatchScript;
extern CommandInfo kCommandInfo_FileExists;

// in Commands_Creature.cpp
extern CommandInfo kCommandInfo_IsCreature;
extern CommandInfo kCommandInfo_GetRider;

// in Commands_Events.cpp, with the handlers they keep
extern CommandInfo kCommandInfo_SetEventHandler;
extern CommandInfo kCommandInfo_RemoveEventHandler;

// in Commands_Game.cpp
extern CommandInfo kCommandInfo_GetCrosshairRef;
extern CommandInfo kCommandInfo_GetGameLoaded;
extern CommandInfo kCommandInfo_GetGameRestarted;
extern CommandInfo kCommandInfo_MessageEX;
extern CommandInfo kCommandInfo_MessageBoxEX;

// in Commands_Input.cpp
extern CommandInfo kCommandInfo_GetNumKeysPressed;
extern CommandInfo kCommandInfo_GetKeyPress;
extern CommandInfo kCommandInfo_IsControlPressed;
extern CommandInfo kCommandInfo_OnControlDown;
extern CommandInfo kCommandInfo_DisableControl;
extern CommandInfo kCommandInfo_EnableControl;
extern CommandInfo kCommandInfo_TapControl;

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

// in Commands_Loops.cpp, with the loops they keep
extern CommandInfo kCommandInfo_SaveIP;
extern CommandInfo kCommandInfo_RestoreIP;
extern CommandInfo kCommandInfo_ForEach;
extern CommandInfo kCommandInfo_Loop;
extern CommandInfo kCommandInfo_Break;
extern CommandInfo kCommandInfo_Continue;

// in Commands_Menu.cpp
extern CommandInfo kCommandInfo_GetActiveMenuSelection;

// in Commands_MiscReference.cpp
extern CommandInfo kCommandInfo_GetOwner;
extern CommandInfo kCommandInfo_GetParentCellOwner;
extern CommandInfo kCommandInfo_GetOwningFactionRequiredRank;
extern CommandInfo kCommandInfo_GetParentCellOwningFactionRequiredRank;
extern CommandInfo kCommandInfo_IsOffLimits;
extern CommandInfo kCommandInfo_GetFirstRef;
extern CommandInfo kCommandInfo_GetNextRef;

// in Commands_String.cpp, with the strings they keep
extern CommandInfo kCommandInfo_sv_Construct;
extern CommandInfo kCommandInfo_sv_Set;
extern CommandInfo kCommandInfo_sv_Length;
extern CommandInfo kCommandInfo_sv_SubString;
extern CommandInfo kCommandInfo_sv_Destruct;

// in FunctionScripts.cpp, with the calls they make
extern CommandInfo kCommandInfo_Call;
extern CommandInfo kCommandInfo_SetFunctionValue;

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
	{ &kCommandInfo_GetHotKeyItem,	kRetnType_Default },
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
